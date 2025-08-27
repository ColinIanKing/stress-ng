/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
 */
#include "stress-ng.h"

#include <time.h>

#if defined(HAVE_SYS_AUXV_H)
#include <sys/auxv.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_LINK_H)
#include <link.h>
#else
UNEXPECTED
#endif

static const stress_help_t help[] = {
	{ NULL,	"vdso N",	"start N workers exercising functions in the VDSO" },
	{ NULL,	"vdso-func F",	"use just vDSO function F" },
	{ NULL,	"vdso-ops N",	"stop after N vDSO function calls" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_vdso_func, "vdso-func", TYPE_ID_STR, 0, 0, NULL },
	END_OPT,
};

#if defined(HAVE_SYS_AUXV_H) && \
    defined(HAVE_LINK_H) && \
    defined(HAVE_GETAUXVAL) && \
    defined(AT_SYSINFO_EHDR)

typedef int (*stress_vdso_func_t)(void *);

/*
 *  Symbol name to wrapper function lookup
 */
typedef struct stress_wrap_func {
	const stress_vdso_func_t func;	/* Wrapper function */
	char *name;			/* Function name */
} stress_wrap_func_t;

/*
 *  vDSO symbol mapping name to address and wrapper function
 */
typedef struct vdso_sym {
	struct vdso_sym *next;	/* Next symbol in list */
	char *name;		/* Function name */
	void *addr;		/* Function address in vDSO */
	stress_vdso_func_t func;/* Wrapper function */
	stress_vdso_func_t dummy_func;	/* Dummy wrapper function for instrumentation */
	bool duplicate;		/* True if a duplicate call */
} stress_vdso_sym_t;

static stress_vdso_sym_t *vdso_sym_list;

/*
 *  wrap_getcpu()
 *	invoke getcpu()
 */
static int OPTIMIZE3 wrap_getcpu(void *vdso_func)
{
	unsigned cpu, node;

	int (*vdso_getcpu)(unsigned *cpu, unsigned *node, void *tcache);

	*(void **)(&vdso_getcpu) = vdso_func;
	return vdso_getcpu(&cpu, &node, NULL);
}

/*
 *  wrap_gettimeofday()
 *	invoke gettimeofday()
 */
static int OPTIMIZE3 wrap_gettimeofday(void *vdso_func)
{
	int (*vdso_gettimeofday)(struct timeval *tv, shim_timezone_t *tz);
	struct timeval tv;

	*(void **)(&vdso_gettimeofday) = vdso_func;
	return vdso_gettimeofday(&tv, NULL);
}

/*
 *  wrap_time()
 *	invoke time()
 */
static int OPTIMIZE3 wrap_time(void *vdso_func)
{
	time_t (*vdso_time)(time_t *tloc);
	time_t t, ret;

	*(void **)(&vdso_time) = vdso_func;
	ret = vdso_time(&t);
	return (ret == ((time_t)-1)) ? -1 : 0;
}

#if defined(HAVE_CLOCK_GETTIME)
/*
 *  wrap_clock_gettime()
 *	invoke clock_gettime()
 */
static int OPTIMIZE3 wrap_clock_gettime(void *vdso_func)
{
	int (*vdso_clock_gettime)(clockid_t clk_id, struct timespec *tp);
	struct timespec tp;

	*(void **)(&vdso_clock_gettime) = vdso_func;
	return vdso_clock_gettime(CLOCK_MONOTONIC, &tp);
}
#endif

#if defined(HAVE_CLOCK_GETRES)
/*
 *  wrap_clock_getres()
 *	invoke clock_getres()
 */
static int OPTIMIZE3 wrap_clock_getres(void *vdso_func)
{
	int (*vdso_clock_getres)(clockid_t clockid, struct timespec *res);
	struct timespec res;

	*(void **)(&vdso_clock_getres) = vdso_func;
	return vdso_clock_getres(CLOCK_MONOTONIC, &res);
}
#endif


/*
 *  stress_dummy()
 *	no-op dummy func
 */
static int OPTIMIZE3 stress_dummy(void)
{
	return 0;
}

/*
 *  wrap_dummy()
 *      dummy empty function for baseline
 */
static int OPTIMIZE3 wrap_dummy(void *vdso_func)
{
	int (*vdso_dummy)(void);

	*(void **)(&vdso_dummy) = vdso_func;

	return (int)stress_dummy();
}

/*
 *  mapping of wrappers to function symbol name
 */
static const stress_wrap_func_t wrap_funcs[] = {
#if defined(HAVE_CLOCK_GETTIME)
	{ wrap_clock_gettime,	"clock_gettime" },
	{ wrap_clock_gettime,	"__vdso_clock_gettime" },
	{ wrap_clock_gettime,	"__kernel_clock_gettime" },
#endif
#if defined(HAVE_CLOCK_GETRES)
	{ wrap_clock_getres,	"clock_getres" },
	{ wrap_clock_getres,	"__vdso_clock_getres" },
	{ wrap_clock_getres,	"__kernel_clock_getres" },
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
static stress_vdso_func_t func_find(const char *name)
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
			ElfW(Dyn *) dyn_start;
			ElfW(Word *) hash = NULL;
			ElfW(Word) j;
			ElfW(Word) buckets = 0;
			const ElfW(Word *) bucket = NULL;
			const ElfW(Word *) chain = NULL;
			char *strtab = NULL;

			if (!load_offset)
				continue;

			if ((void *)info->dlpi_addr != vdso)
				continue;

			dyn_start = (ElfW(Dyn)*)(info->dlpi_addr +  info->dlpi_phdr[i].p_vaddr);

			/*
			 *  Find hash table and strtab first
			 */
			for (dyn = dyn_start; dyn->d_tag != DT_NULL; dyn++) {
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
				default:
					break;
				}
			}

			/* No hash table or strtab, abort symbol search */
			if ((!hash) || (!strtab))
				return 0;

			for (dyn = dyn_start; dyn->d_tag != DT_NULL; dyn++) {
				if (dyn->d_tag == DT_SYMTAB) {
					ElfW(Sym *) symtab = (ElfW(Sym *))(dyn->d_un.d_ptr + info->dlpi_addr);

					/*
					 *  Scan through all the chains in each bucket looking
					 *  for relevant symbols
					 */
					for (j = 0; j < buckets; j++) {
						ElfW(Word) ch;

						for (ch = bucket[j]; ch != STN_UNDEF; ch = chain[ch]) {
							const ElfW(Sym) *sym = &symtab[ch];
							stress_vdso_sym_t *vdso_sym;
							char *name = strtab + sym->st_name;
							stress_vdso_func_t func;

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
							vdso_sym = (stress_vdso_sym_t *)calloc(1, sizeof(*vdso_sym));
							if (vdso_sym == NULL)
								return -1;

							vdso_sym->name = name;
							vdso_sym->addr = (void *)(sym->st_value + (uintptr_t)load_offset);
							vdso_sym->func = func;
							vdso_sym->dummy_func = wrap_dummy;
							vdso_sym->next = vdso_sym_list;
							vdso_sym_list = vdso_sym;
						}
					}
				}
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
	stress_vdso_sym_t *vdso_sym;

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
			(void)shim_strlcat(tmp, " ", len);
		}
		(void)shim_strlcat(tmp, vdso_sym->name, len);
		str = tmp;
	}
	return str;
}

/*
 *  vdso_sym_list_free()
 *	free up the symbols
 */
static void vdso_sym_list_free(stress_vdso_sym_t **list)
{
	stress_vdso_sym_t *vdso_sym = *list;

	while (vdso_sym) {
		stress_vdso_sym_t *next = vdso_sym->next;

		free(vdso_sym);
		vdso_sym = next;
	}
	*list = NULL;
}

/*
 *  remove_sym
 *	find and remove a symbol from the symbol list
 */
static void remove_sym(stress_vdso_sym_t **list, stress_vdso_sym_t *dup)
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
static void vdso_sym_list_remove_duplicates(stress_vdso_sym_t **list)
{
	stress_vdso_sym_t *vs1;

	for (vs1 = *list; vs1; vs1 = vs1->next) {
		stress_vdso_sym_t *vs2;

		if (vs1->name[0] == '_') {
			for (vs2 = *list; vs2; vs2 = vs2->next) {
				if ((vs1 != vs2) && (vs1->addr == vs2->addr))
					vs1->duplicate = true;
			}
		}
	}

	vs1 = *list;
	while (vs1) {
		stress_vdso_sym_t *next = vs1->next;

		if (vs1->duplicate)
			remove_sym(list, vs1);
		vs1 = next;
	}
}

/*
 *  stress_vdso_supported()
 *	early sanity check to see if functionality is supported
 */
static int stress_vdso_supported(const char *name)
{
	unsigned long int vdso = getauxval(AT_SYSINFO_EHDR);

	if (vdso == 0) {
		pr_inf_skip("%s stressor will be skipped, failed to find vDSO address\n", name);
		return -1;
	}

	vdso_sym_list = NULL;
	dl_iterate_phdr(dl_wrapback, (void *)vdso);

	if (!vdso_sym_list) {
		pr_inf_skip("%s stressor will be skipped, failed to find relevant vDSO "
			"functions\n", name);
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
static int vdso_sym_list_check_vdso_func(stress_vdso_sym_t **list)
{
	stress_vdso_sym_t *vs1;
	char *name;

	if (!stress_get_setting("vdso-func", &name))
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
		stress_vdso_sym_t *next = vs1->next;

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
static int stress_vdso(stress_args_t *args)
{
	double t1, t2, t3, dt, overhead_ns;
	uint64_t counter;
	int n_vdso = 0;
	register stress_vdso_sym_t *vdso_sym;

	if (!vdso_sym_list) {
		/* Should not fail, but worth checking to avoid breakage */
		if (stress_instance_zero(args))
			pr_inf_skip("%s: could not find any vDSO functions, skipping\n",
				args->name);
		return EXIT_NOT_IMPLEMENTED;
	}
	vdso_sym_list_remove_duplicates(&vdso_sym_list);
	if (vdso_sym_list_check_vdso_func(&vdso_sym_list) < 0) {
		return EXIT_FAILURE;
	}

	if (stress_instance_zero(args)) {
		char *str;

		str = vdso_sym_list_str();
		if (str) {
			pr_inf("%s: exercising vDSO functions: %s\n",
				args->name, str);
			free(str);
		}
	}

	for (vdso_sym = vdso_sym_list; vdso_sym; vdso_sym = vdso_sym->next)
		n_vdso++;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t1 = stress_time_now();
	do {
		for (vdso_sym = vdso_sym_list; vdso_sym; vdso_sym = vdso_sym->next) {
			vdso_sym->func(vdso_sym->addr);
		}
		stress_bogo_add(args, n_vdso);
	} while (stress_continue(args));
	t2 = stress_time_now();

	counter = stress_bogo_get(args);

	do {
		int j;

		for (j = 0; j < 1000000; j++) {
			for (vdso_sym = vdso_sym_list; vdso_sym; vdso_sym = vdso_sym->next) {
				vdso_sym->dummy_func(vdso_sym->addr);
			}
			stress_bogo_add(args, n_vdso);
		}
		t3 = stress_time_now();
	} while ((t3 - t2) < 0.1);

	overhead_ns = (double)STRESS_NANOSECOND * ((t3 - t2) / (double)(stress_bogo_get(args) - counter));
	stress_bogo_set(args, counter);

	dt = t2 - t1;
	if (dt > 0.0) {
		const double ns = ((dt * (double)STRESS_NANOSECOND) / (double)counter) - overhead_ns;

		stress_metrics_set(args, 0, "nanosecs per call (excluding test overhead)",
			ns, STRESS_METRIC_HARMONIC_MEAN);
		stress_metrics_set(args, 1, "nanosecs for test overhead",
			overhead_ns, STRESS_METRIC_GEOMETRIC_MEAN);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	vdso_sym_list_free(&vdso_sym_list);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_vdso_info = {
	.stressor = stress_vdso,
	.supported = stress_vdso_supported,
	.classifier = CLASS_OS,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_vdso_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without sys/auxv.h, link.h, getauxval() or AT_SYSINFO_EHDR defined"
};
#endif
