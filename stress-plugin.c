// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

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
static void *stress_plugin_so;

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

	for (i = 1; stress_continue_flag() && (i < stress_plugin_methods_num); i++) {
		ret = stress_plugin_methods[i].func();
		if (ret)
			break;
	}
	return ret;
}

/*
 *  stress_set_plugin_so()
 *     set default plugin shared object file
 */
static int stress_set_plugin_so(const char *opt)
{
	struct link_map *map = NULL;
	Elf64_Sym * symtab = NULL;
	ElfW(Dyn) *section;
	char * strtab = NULL;
	unsigned long symentries = 0;
	size_t i, size, n_funcs;

	stress_plugin_methods = NULL;
	stress_plugin_methods_num = 0;

	stress_plugin_so = dlopen(opt, RTLD_LAZY | RTLD_GLOBAL);
	if (!stress_plugin_so) {
		fprintf(stderr, "plugin-so: cannot load shared object file %s (please specify full path to .so file)\n", opt);
		return -1;
	}

	dlinfo(stress_plugin_so, RTLD_DI_LINKMAP, &map);

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
		fprintf(stderr, "plugin-so: cannot find symbol table in file %s\n", opt);
		return -1;
	}
	if (!strtab) {
		fprintf(stderr, "plugin-so: cannot find string table in file %s\n", opt);
		return -1;
	}
	if (!symentries) {
		fprintf(stderr, "plugin-so: cannot find symbol table entry count in file %s\n", opt);
		return -1;
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
		fprintf(stderr, "plugin-so: cannot find any function symbols in file %s\n", opt);
		return -1;
	}

	stress_plugin_methods = calloc(n_funcs + 1, sizeof(*stress_plugin_methods));
	if (!stress_plugin_methods) {
		fprintf(stderr, "plugin-so: cannot allocate %zu plugin methods\n", n_funcs);
		return -1;
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
				stress_plugin_methods[n_funcs].func = (stress_plugin_func)dlsym(stress_plugin_so, str);
				if (!stress_plugin_methods[n_funcs].func) {
					fprintf(stderr, "plugin-so: cannot get address of function %s()\n", str);
					return -1;
				}
				n_funcs++;
			}
		}
	}
	stress_plugin_methods_num = n_funcs;

	return 0;
}

/*
 *  stress_set_plugin_method()
 *      set default plugin stress method
 */
static int stress_set_plugin_method(const char *name)
{
	size_t i;

	if (!stress_plugin_methods) {
		pr_inf("plugin-method: no plugin methods found, need to first specify a valid shared library with --plug-so\n");
		return -1;
	}
	if (!stress_plugin_methods_num) {
		pr_inf("plugin-method: no plugin methods found, need to have stress_*() named functions in a valid shared shared library\n");
		return -1;
	}

	for (i = 0; i < stress_plugin_methods_num; i++) {
		if (!strcmp(stress_plugin_methods[i].name, name)) {
			stress_set_setting("plugin-method", TYPE_ID_SIZE_T, &i);
			return 0;
		}
	}

	(void)fprintf(stderr, "plugin-method must be one of:");
	for (i = 0; i < stress_plugin_methods_num; i++) {
		(void)fprintf(stderr, " %s", stress_plugin_methods[i].name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_plugin
 *	stress with random plugins
 */
static int stress_plugin(const stress_args_t *args)
{
	int rc;
	size_t i;
	size_t plugin_method = 0;
	stress_plugin_func func;
	const size_t sig_count_size = MAX_SIGS * sizeof(*sig_count);
	bool report_sigs;

	if (!stress_plugin_so) {
		if (args->instance == 0)
			pr_inf_skip("%s: plugin shared library failed to open, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	(void)stress_get_setting("plugin-method", &plugin_method);
	if (!stress_plugin_methods) {
		if (args->instance == 0)
			pr_inf("%s: no plugin methods found, need to specify a valid shared library with --plug-so\n",
				args->name);
		(void)dlclose(stress_plugin_so);
		return EXIT_NO_RESOURCE;
	}
	if (plugin_method > stress_plugin_methods_num) {
		if (args->instance == 0)
			pr_inf("%s: invalid plugin method index %zd, expecting 0..%zd\n",
				args->name, plugin_method, stress_plugin_methods_num);
		(void)dlclose(stress_plugin_so);
		return EXIT_NO_RESOURCE;
	}

	sig_count = (uint64_t *)mmap(NULL, sig_count_size, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (sig_count == MAP_FAILED) {
		pr_fail("%s: mmap failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)dlclose(stress_plugin_so);
		return EXIT_NO_RESOURCE;
	}

	func = stress_plugin_methods[plugin_method].func;
	if (args->instance == 0)
		pr_dbg("%s: exercising plugin method '%s'\n", args->name, stress_plugin_methods[plugin_method].name);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;

again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (!stress_continue(args))
				goto finish;
			pr_fail("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			goto err;
		}
		if (pid == 0) {
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
			int ret, status;

			ret = shim_waitpid(pid, &status, 0);
			if (ret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid(): errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				stress_force_killed_bogo(args);
				(void)shim_kill(pid, SIGTERM);
				(void)shim_kill(pid, SIGKILL);
				(void)shim_waitpid(pid, &status, 0);
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
	(void)dlclose(stress_plugin_so);
	(void)munmap(sig_count, sig_count_size);
	return rc;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_plugin_method,	stress_set_plugin_method },
	{ OPT_plugin_so,	stress_set_plugin_so },
	{ 0,			NULL }
};

stressor_info_t stress_plugin_info = {
	.stressor = stress_plugin,
	.class = CLASS_CPU | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.supported = stress_plugin_supported,
	.help = help
};

#else

static int stress_set_plugin_ignored(const char *opt)
{
	(void)opt;

	return 0;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_plugin_method,	stress_set_plugin_ignored },
	{ OPT_plugin_so,	stress_set_plugin_ignored },
	{ 0,			NULL }
};

stressor_info_t stress_plugin_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help,
	.unimplemented_reason = "built without link.h, dlfcn.h or built as a static image"
};

#endif
