/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static const help_t help[] = {
	{ NULL,	"vdso N",	"start N workers exercising functions in the VDSO" },
	{ NULL,	"vdso-ops N",	"stop after N vDSO function calls" },
	{ NULL,	"vdso-func F",	"use just vDSO function F" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_set_vdso_func()
 *      set the default vdso function
 */
static int stress_set_vdso_func(const char *name)
{
	return set_setting("vdso-func", TYPE_ID_STR, name);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_vdso_func,	stress_set_vdso_func },
	{ 0,			NULL }
};

#if defined(HAVE_SYS_AUXV_H) && \
    defined(HAVE_LINK_H) && \
    defined(HAVE_GETAUXVAL) && \
    defined(AT_SYSINFO_EHDR)

typedef void (*func_t)(void *);

/*
 *  Symbol name to wrapper function lookup
 */
typedef struct wrap_func {
	func_t func;		/* Wrapper function */
	char *name;		/* Function name */
} wrap_func_t;

/*
 *  vDSO symbol mapping name to address and wrapper function
 */
typedef struct vdso_sym {
	struct vdso_sym *next;	/* Next symbol in list */
	char *name;		/* Function name */
	void *addr;		/* Function address in vDSO */
	func_t func;		/* Wrapper function */
	bool duplicate;		/* True if a duplicate call */
} vdso_sym_t;

static vdso_sym_t *vdso_sym_list;

/*
 *  wrap_getcpu()
 *	invoke getcpu()
 */
static void wrap_getcpu(void *vdso_func)
{
	unsigned cpu, node;

	int (*vdso_getcpu)(unsigned *cpu, unsigned *node, void *tcache);

	*(void **)(&vdso_getcpu) = vdso_func;
	(void)vdso_getcpu(&cpu, &node, NULL);
}

/*
 *  wrap_gettimeofday()
 *	invoke gettimeofday()
 */
static void wrap_gettimeofday(void *vdso_func)
{
	int (*vdso_gettimeofday)(struct timeval *tv, struct timezone *tz);
	struct timeval tv;

	*(void **)(&vdso_gettimeofday) = vdso_func;
	(void)vdso_gettimeofday(&tv, NULL);
}

/*
 *  wrap_time()
 *	invoke time()
 */
static void wrap_time(void *vdso_func)
{
	time_t (*vdso_time)(time_t *tloc);
	time_t t;

	*(void **)(&vdso_time) = vdso_func;
	(void)vdso_time(&t);
}

#if defined(HAVE_CLOCK_GETTIME)
/*
 *  wrap_clock_gettime()
 *	invoke clock_gettime()
 */
static void wrap_clock_gettime(void *vdso_func)
{
	int (*vdso_clock_gettime)(clockid_t clk_id, struct timespec *tp);
	struct timespec tp;

	*(void **)(&vdso_clock_gettime) = vdso_func;
	vdso_clock_gettime(CLOCK_MONOTONIC, &tp);
}
#endif

/*
 *  mapping of wrappers to function symbol name
 */
static wrap_func_t wrap_funcs[] = {
#if defined(HAVE_CLOCK_GETTIME)
	{ wrap_clock_gettime,	"clock_gettime" },
	{ wrap_clock_gettime,	"__vdso_clock_gettime" },
	{ wrap_clock_gettime,	"__kernel_clock_gettime" },
#endif
	{ wrap_getcpu,		"getcpu" },
	{ wrap_getcpu,		"__vdso_getcpu" },
	{ wrap_getcpu,		"__kernel_getcpu" },
	{ wrap_gettimeofday,	"gettimeofday" },
	{ wrap_gettimeofday,	"__vdso_gettimeofday" },
	{ wrap_gettimeofday,	"__kernel_gettimeofday" },
	{ wrap_time,		"time" },
	{ wrap_time,		"__vdso_time" },
	{ wrap_time,		"__kernel_time" },
};

/*
 *  func_find()
 *	find wrapper function by symbol name
 */
static func_t func_find(char *name)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(wrap_funcs); i++) {
		if (!strcmp(name, wrap_funcs[i].name))
			return wrap_funcs[i].func;
	}
	return NULL;
}

/*
 *  dl_wrapback()
 *	find vDSO symbols
 */
static int dl_wrapback(struct dl_phdr_info* info, size_t info_size, void *vdso)
{
	ElfW(Word) i;
	void *load_offset = NULL;

	(void)info_size;

	for (i = 0; i < info->dlpi_phnum; i++) {
		if (info->dlpi_phdr[i].p_type == PT_LOAD) {
			load_offset = (void *)(info->dlpi_addr +
                                + (uintptr_t)info->dlpi_phdr[i].p_offset
                                - (uintptr_t)info->dlpi_phdr[i].p_vaddr);

		}
		if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
			ElfW(Dyn *) dyn;
			ElfW(Word *) hash = NULL;
			ElfW(Word) j;
			ElfW(Word) buckets = 0;
			ElfW(Sym *) symtab;
			ElfW(Word *) bucket = NULL;
			ElfW(Word *) chain = NULL;
			char *strtab = NULL;

			if (!load_offset)
				continue;

			if ((void *)info->dlpi_addr != vdso)
				continue;

			dyn = (ElfW(Dyn)*)(info->dlpi_addr +  info->dlpi_phdr[i].p_vaddr);

			while (dyn->d_tag != DT_NULL) {
				switch (dyn->d_tag) {
				case DT_HASH:
					hash = (ElfW(Word *))(dyn->d_un.d_ptr + info->dlpi_addr);
					buckets = hash[0];
					bucket = &hash[2];
					chain = &hash[buckets + 2];
					break;

				case DT_STRTAB:
					strtab = (char *)(dyn->d_un.d_ptr + info->dlpi_addr);
					break;

				case DT_SYMTAB:
					symtab = (ElfW(Sym *))(dyn->d_un.d_ptr + info->dlpi_addr);

					if ((!hash) || (!strtab))
						break;

					/*
					 *  Scan through all the chains in each bucket looking
					 *  for relevant symbols
					 */
					for (j = 0; j < buckets; j++) {
						ElfW(Word) ch;

						for (ch = bucket[j]; ch != STN_UNDEF; ch = chain[ch]) {
							ElfW(Sym) *sym = &symtab[ch];
							vdso_sym_t *vdso_sym;
							char *name = strtab + sym->st_name;
							func_t func;

							if ((ELF64_ST_TYPE(sym->st_info) != STT_FUNC) ||
							    ((ELF64_ST_BIND(sym->st_info) != STB_GLOBAL) &&
							     (ELF64_ST_BIND(sym->st_info) != STB_WEAK)) ||
							    (sym->st_shndx == SHN_UNDEF))
								continue;

							/*
							 *  Do we have a wrapper for this function?
							 */
							func = func_find(name);
							if (!func)
								continue;

							/*
							 *  Add to list of wrapable vDSO functions
							 */
							vdso_sym = calloc(1, sizeof(*vdso_sym));
							if (vdso_sym == NULL)
								return -1;

							vdso_sym->name = name;
							vdso_sym->addr = sym->st_value + load_offset;
							vdso_sym->func = func;
							vdso_sym->next = vdso_sym_list;
							vdso_sym_list = vdso_sym;
						}
					}
					break;
				}
				dyn++;
			}
		}
	}
	return 0;
}

/*
 *  vdso_sym_list_str()
 *	gather symbol names into a string
 */
static char *vdso_sym_list_str(void)
{
	char *str = NULL;
	size_t len = 0;
	vdso_sym_t *vdso_sym;

	for (vdso_sym = vdso_sym_list; vdso_sym; vdso_sym = vdso_sym->next) {
		char *tmp;
		len += (strlen(vdso_sym->name) + 2);
		tmp = realloc(str, len);
		if (!tmp) {
			free(str);
			return NULL;
		}
		if (!str) {
			*tmp = '\0';
		} else {
			(void)strcat(tmp, " ");
			str = tmp;
		}
		(void)strcat(tmp, vdso_sym->name);
		str = tmp;
	}
	return str;
}

/*
 *  vdso_sym_list_free()
 *	free up the symbols
 */
static void vdso_sym_list_free(vdso_sym_t **list)
{
	vdso_sym_t *vdso_sym = *list;

	while (vdso_sym) {
		vdso_sym_t *next = vdso_sym->next;

		free(vdso_sym);
		vdso_sym = next;
	}
	*list = NULL;
}

/*
 *  remove_sym
 *	find and remove a symbol from the symbol list
 */
static void remove_sym(vdso_sym_t **list, vdso_sym_t *dup)
{
	while (*list) {
		if (*list == dup) {
			*list = dup->next;
			free(dup);
			return;
		}
		list = &(*list)->next;
	}
}

/*
 *  vdso_sym_list_remove_duplicates()
 *	remove duplicated system calls
 */
static void vdso_sym_list_remove_duplicates(vdso_sym_t **list)
{
	vdso_sym_t *vs1;

	for (vs1 = *list; vs1; vs1 = vs1->next) {
		vdso_sym_t *vs2;

		if (vs1->name[0] == '_') {
			for (vs2 = *list; vs2; vs2 = vs2->next) {
				if ((vs1 != vs2) && (vs1->addr == vs2->addr))
					vs1->duplicate = true;
			}
		}
	}

	vs1 = *list;
	while (vs1) {
		vdso_sym_t *next = vs1->next;

		if (vs1->duplicate)
			remove_sym(list, vs1);
		vs1 = next;
	}
}

/*
 *  stress_vdso_supported()
 *	early sanity check to see if functionality is supported
 */
static int stress_vdso_supported(void)
{
	void *vdso = (void *)getauxval(AT_SYSINFO_EHDR);

	if (vdso == NULL) {
		pr_inf("vdso stressor will be skipped, failed to find vDSO address\n");
		return -1;
	}

	vdso_sym_list = NULL;
	dl_iterate_phdr(dl_wrapback, vdso);

	if (!vdso_sym_list) {
		pr_inf("vsdo stressor will be skipped, failed to find relevant vDSO functions\n");
		return -1;
	}

	return 0;
}

/*
 *  vdso_sym_list_check_vdso_func()
 *	if a vdso-func has been specified, locate it and
 *	remove all other symbols from the list so just
 *	this one function is used.
 */
static int vdso_sym_list_check_vdso_func(vdso_sym_t **list)
{
	vdso_sym_t *vs1;
	char *name;

	if (!get_setting("vdso-func", &name))
		return 0;

	for (vs1 = vdso_sym_list; vs1; vs1 = vs1->next) {
		if (!strcmp(vs1->name, name))
			break;
	}
	if (!vs1) {
		(void)fprintf(stderr, "invalid vdso-func '%s', must be one of:", name);
		for (vs1 = vdso_sym_list; vs1; vs1 = vs1->next)
                	(void)fprintf(stderr, " %s", vs1->name);
        	(void)fprintf(stderr, "\n");
		return -1;
        }

	vs1 = *list;
	while (vs1) {
		vdso_sym_t *next = vs1->next;

		if (strcmp(vs1->name, name))
			remove_sym(list, vs1);
		vs1 = next;
	}
	return 0;
}

/*
 *  stress_vdso()
 *	stress system wraps in vDSO
 */
static int stress_vdso(const args_t *args)
{
	char *str;
	double t1, t2;

	if (!vdso_sym_list) {
		/* Should not fail, but worth checking to avoid breakage */
		pr_inf("%s: could not find any vDSO functions, skipping\n",
			args->name);
		return EXIT_NOT_IMPLEMENTED;
	}
	vdso_sym_list_remove_duplicates(&vdso_sym_list);
	if (vdso_sym_list_check_vdso_func(&vdso_sym_list) < 0) {
		return EXIT_FAILURE;
	}

	if (args->instance == 0) {
		str = vdso_sym_list_str();
		if (str) {
			pr_inf("%s: exercising vDSO functions: %s\n",
				args->name, str);
			free(str);
		}
	}

	t1 = time_now();
	do {
		vdso_sym_t *vdso_sym;

		for (vdso_sym = vdso_sym_list; vdso_sym; vdso_sym = vdso_sym->next) {
			vdso_sym->func(vdso_sym->addr);
			inc_counter(args);
		}
	} while (keep_stressing());
	t2 = time_now();

	pr_inf("%s: %.2f nanoseconds per call\n",
		args->name,
		((t2 - t1) * 1000000000.0) / (double)get_counter(args));

	vdso_sym_list_free(&vdso_sym_list);

	return EXIT_SUCCESS;
}

stressor_info_t stress_vdso_info = {
	.stressor = stress_vdso,
	.supported = stress_vdso_supported,
	.class = CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_vdso_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
