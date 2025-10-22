/*
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
#include "core-capabilities.h"
#include "core-killpid.h"
#include "core-mmap.h"

#if defined(HAVE_LINK_H)
#include <link.h>
#endif
#if defined(HAVE_LIB_DL)
#include <dlfcn.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"plugin N",	   "start N workers exercising random plugins" },
	{ NULL,	"plugin-method M", "set plugin stress method" },
	{ NULL,	"plugin-ops N",	   "stop after N plugin bogo operations" },
	{ NULL, "plugin-so file",  "specify plugin shared object file" },
	{ NULL, NULL,		   NULL }
};

#if defined(HAVE_LINK_H) &&	\
    defined(HAVE_LIB_DL) &&	\
    !defined(BUILD_STATIC)

typedef int (*stress_plugin_func)(void);

typedef struct {
	const char *name;
	stress_plugin_func func;
} stress_plugin_method_info_t;

static stress_plugin_method_info_t *stress_plugin_methods;
static size_t stress_plugin_methods_num;
static void *stress_plugin_so_dl;

typedef struct {
	const int signum;	/* Signal number */
	const bool report;	/* true - report signal being handled */
} sig_report_t;

static const sig_report_t sig_report[] = {
#if defined(SIGILL)
	{ SIGILL,	true },
#endif
#if defined(SIGTRAP)
	{ SIGTRAP,	true },
#endif
#if defined(SIGFPE)
	{ SIGFPE,	true },
#endif
#if defined(SIGBUS)
	{ SIGBUS,	true },
#endif
#if defined(SIGSEGV)
	{ SIGSEGV,	true },
#endif
#if defined(SIGIOT)
	{ SIGIOT,	true },
#endif
#if defined(SIGEMT)
	{ SIGEMT,	true },
#endif
#if defined(SIGALRM)
	{ SIGALRM,	false },
#endif
#if defined(SIGINT)
	{ SIGINT,	false },
#endif
#if defined(SIGHUP)
	{ SIGHUP,	false },
#endif
#if defined(SIGSYS)
	{ SIGSYS,	true },
#endif
};

#if defined(NSIG)
#define MAX_SIGS	(NSIG)
#elif defined(_NSIG)
#define MAX_SIGS	(_NSIG)
#else
#define MAX_SIGS	(256)
#endif

static uint64_t *sig_count;

static int stress_plugin_supported(const char *name)
{
	if (stress_plugin_methods_num == 0) {
		pr_inf_skip("%s: no plugin-so specified, skipping stressor\n", name);
		return -1;
	}
	return 0;
}

static bool stress_plugin_report_signum(const int signum)
{
	register size_t i;

	for (i = 0; i < SIZEOF_ARRAY(sig_report); i++)
		if ((sig_report[i].signum == signum) && (sig_report[i].report))
			return true;

	return false;
}

static void MLOCKED_TEXT NORETURN stress_sig_handler(int signum)
{
	if (signum < MAX_SIGS)
		sig_count[signum]++;

	_exit(1);
}

static int stress_plugin_method_all(void)
{
	register size_t i;
	register int ret = 0;

	for (i = 1; LIKELY(stress_continue_flag() && (i < stress_plugin_methods_num)); i++) {
		ret = stress_plugin_methods[i].func();
		if (ret)
			break;
	}
	return ret;
}

/*
 *  stress_plugin_so()
 *     set default plugin shared object file
 */
static void stress_plugin_so(const char *opt_name, const char *opt_arg, stress_type_id_t *type_id, void *value)
{
	struct link_map *map = NULL;
	Elf64_Sym * symtab = NULL;
	ElfW(Dyn) *section;
	char * strtab = NULL;
	unsigned long int symentries = 0;
	size_t i, size, n_funcs;

	stress_plugin_methods = NULL;
	stress_plugin_methods_num = 0;

	*type_id = TYPE_ID_STR;
	*(char **)value = stress_const_optdup(opt_arg);

	stress_plugin_so_dl = dlopen(opt_arg, RTLD_LAZY | RTLD_GLOBAL);
	if (!stress_plugin_so_dl) {
		fprintf(stderr, "option %s: cannot load shared object file %s "
			"(please specify full path to .so file)\n", opt_name, opt_arg);
		longjmp(g_error_env, 1);
		stress_no_return();
	}

	dlinfo(stress_plugin_so_dl, RTLD_DI_LINKMAP, &map);

	for (section = map->l_ld; section->d_tag != DT_NULL; ++section) {
		switch (section->d_tag) {
		case DT_SYMTAB:
			symtab = (Elf64_Sym *)section->d_un.d_ptr;
			break;
		case DT_STRTAB:
			strtab = (char *)section->d_un.d_ptr;
			break;
		case DT_SYMENT:
			symentries = section->d_un.d_val;
			break;
		}
	}

	if (!symtab) {
		fprintf(stderr, "plugin-so: cannot find symbol table in file %s\n", opt_arg);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	if (!strtab) {
		fprintf(stderr, "plugin-so: cannot find string table in file %s\n", opt_arg);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	if (!symentries) {
		fprintf(stderr, "plugin-so: cannot find symbol table entry count in file %s\n", opt_arg);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	size = (size_t)(strtab - (char *)symtab);

	for (n_funcs = 0, i = 0; i < size / symentries; i++) {
		if (ELF64_ST_TYPE(symtab[i].st_info) == STT_FUNC) {
			const Elf64_Sym *sym = &symtab[i];
			const char *str = &strtab[sym->st_name];

			if (!strncmp(str, "stress_", 7))
				n_funcs++;
		}
	}
	if (!n_funcs) {
		fprintf(stderr, "plugin-so: cannot find any function symbols in file %s\n", opt_arg);
		longjmp(g_error_env, 1);
		stress_no_return();
	}

	stress_plugin_methods = (stress_plugin_method_info_t *)calloc(n_funcs + 1, sizeof(*stress_plugin_methods));
	if (!stress_plugin_methods) {
		fprintf(stderr, "plugin-so: cannot allocate %zu plugin methods%s\n",
			n_funcs, stress_get_memfree_str());
		longjmp(g_error_env, 1);
		stress_no_return();
	}

	n_funcs = 0;
	stress_plugin_methods[n_funcs].name = "all";
	stress_plugin_methods[n_funcs].func = stress_plugin_method_all;
	n_funcs++;

	for (i = 0; i < size / symentries; i++) {
		if (ELF64_ST_TYPE(symtab[i].st_info) == STT_FUNC) {
			const Elf64_Sym *sym = &symtab[i];
			const char *str = &strtab[sym->st_name];

			if ((strlen(str) > 7) && !strncmp(str, "stress_", 7)) {
				stress_plugin_methods[n_funcs].name = str + 7;
				stress_plugin_methods[n_funcs].func = (stress_plugin_func)dlsym(stress_plugin_so_dl, str);
				if (!stress_plugin_methods[n_funcs].func) {
					fprintf(stderr, "plugin-so: cannot get address of function %s()\n", str);
					longjmp(g_error_env, 1);
					stress_no_return();
				}
				n_funcs++;
			}
		}
	}
	stress_plugin_methods_num = n_funcs;
}

/*
 *  stress_plugin
 *	stress with random plugins
 */
static int stress_plugin(stress_args_t *args)
{
	int rc;
	size_t i;
	size_t plugin_method = 0;
	stress_plugin_func func;
	const size_t sig_count_size = MAX_SIGS * sizeof(*sig_count);
	bool report_sigs;

	if (!stress_plugin_so_dl) {
		if (stress_instance_zero(args))
			pr_inf_skip("%s: plugin shared library failed to open, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	(void)stress_get_setting("plugin-method", &plugin_method);
	if (!stress_plugin_methods) {
		if (stress_instance_zero(args))
			pr_inf("%s: no plugin methods found, need to specify a valid shared library with --plug-so\n",
				args->name);
		(void)dlclose(stress_plugin_so_dl);
		return EXIT_NO_RESOURCE;
	}
	if (plugin_method > stress_plugin_methods_num) {
		if (stress_instance_zero(args))
			pr_inf("%s: invalid plugin method index %zd, expecting 0..%zd\n",
				args->name, plugin_method, stress_plugin_methods_num);
		(void)dlclose(stress_plugin_so_dl);
		return EXIT_NO_RESOURCE;
	}

	sig_count = (uint64_t *)stress_mmap_populate(NULL, sig_count_size,
		PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (sig_count == MAP_FAILED) {
		pr_fail("%s: failed to mmap %zu bytes%s, errno=%d (%s)\n",
			args->name, sig_count_size,
			stress_get_memfree_str(), errno, strerror(errno));
		(void)dlclose(stress_plugin_so_dl);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(sig_count, sig_count_size, "signal-counters");

	func = stress_plugin_methods[plugin_method].func;
	if (stress_instance_zero(args))
		pr_dbg("%s: exercising plugin method '%s'\n", args->name, stress_plugin_methods[plugin_method].name);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;

again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (UNLIKELY(!stress_continue(args)))
				goto finish;
			pr_fail("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			goto err;
		}
		if (pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			(void)sched_settings_apply(true);

			/* We don't want core dumps either */
			stress_process_dumpable(false);

			/* Drop all capabilities */
			if (stress_drop_capabilities(args->name) < 0) {
				_exit(EXIT_NO_RESOURCE);
			}
			for (i = 0; i < SIZEOF_ARRAY(sig_report); i++) {
				if (stress_sighandler(args->name, sig_report[i].signum, stress_sig_handler, NULL) < 0)
					_exit(EXIT_FAILURE);
			}

			/* Disable stack smashing messages */
			stress_set_stack_smash_check_flag(false);

			do {
				if (func())
					break;
				stress_bogo_inc(args);
			} while (stress_continue(args));
			_exit(0);
		}
		if (pid > 0) {
			pid_t ret;
			int status;

			ret = shim_waitpid(pid, &status, 0);
			if (ret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid() on PID %" PRIdMAX " failed, errno=%d (%s)\n",
						args->name, (intmax_t)pid, errno, strerror(errno));
				stress_force_killed_bogo(args);
				(void)stress_kill_pid_wait(pid, NULL);
			}
		}
	} while (stress_continue(args));

finish:
	rc = EXIT_SUCCESS;

	for (report_sigs = false, i = 0; i < MAX_SIGS; i++) {
		if (sig_count[i] && stress_plugin_report_signum((int)i)) {
			report_sigs = true;
			break;
		}
	}

	if (report_sigs) {
		pr_inf("%s: NOTE: Caught unexpected signal(s):\n", args->name);
		for (i = 0; i < MAX_SIGS; i++) {
			if (sig_count[i] && stress_plugin_report_signum((int)i)) {
				pr_dbg("%s:   %-25.25sx %" PRIu64 "\n",
					args->name, strsignal((int)i), sig_count[i]);
			}
		}
	}
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(stress_plugin_methods);
	(void)dlclose(stress_plugin_so_dl);
	(void)munmap((void *)sig_count, sig_count_size);
	return rc;
}

static const char *stress_plugin_method(const size_t i)
{
	static bool warned = false;

	if (warned)
		return NULL;
	if (!stress_plugin_methods) {
		pr_inf("plugin-method: no plugin methods found, need to first specify a valid shared library with --plug-so\n");
		warned = true;
		return NULL;
	}
	if (!stress_plugin_methods_num) {
		pr_inf("plugin-method: no plugin methods found, need to have stress_*() named functions in a valid shared shared library\n");
		warned = true;
		return NULL;
	}
	return (i < stress_plugin_methods_num) ? stress_plugin_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_plugin_method, "plugin-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_plugin_method },
	{ OPT_plugin_so,     "plugin-so",     TYPE_ID_CALLBACK, 0, 0, stress_plugin_so },
	END_OPT,
};

const stressor_info_t stress_plugin_info = {
	.stressor = stress_plugin,
	.classifier = CLASS_CPU | CLASS_OS,
	.opts = opts,
	.supported = stress_plugin_supported,
	.help = help
};

#else

static void stress_plugin_so(const char *opt_name, const char *opt_arg, stress_type_id_t *type_id, void *value)
{
	*type_id = TYPE_ID_STR;
	*(char **)value = stress_const_optdup(opt_arg);

	fprintf(stderr, "option %s '%s' not supported on unimplemented stressor\n", opt_name, opt_arg);
}

static const stress_opt_t opts[] = {
	{ OPT_plugin_method, "plugin-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_unimplemented_method },
	{ OPT_plugin_so,     "plugin-so",     TYPE_ID_CALLBACK, 0, 0, stress_plugin_so },
	END_OPT,
};

const stressor_info_t stress_plugin_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_OS,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without link.h, dlfcn.h or built as a static image"
};

#endif
