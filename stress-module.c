/*
 * Copyright (C) 2023-2024 Luis Chamberlain <mcgrof@kernel.org>
 * Copyright (C) 2023-2024 Colin Ian King.
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
#include "core-attribute.h"
#include "core-capabilities.h"
#include "core-module.h"

#include <ctype.h>

#if defined(HAVE_LINUX_MODULE_H)
#include <linux/module.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_LIBGEN_H)
#include <libgen.h>
#endif

#if defined(HAVE_SYS_UTSNAME_H)
#include <sys/utsname.h>
#else
UNEXPECTED
#endif

#ifndef MODULE_INIT_IGNORE_MODVERSIONS
#define MODULE_INIT_IGNORE_MODVERSIONS 1
#endif

#ifndef MODULE_INIT_IGNORE_VERMAGIC
#define MODULE_INIT_IGNORE_VERMAGIC 2
#endif

static const stress_help_t help[] = {
	{ NULL,	"module N",	    "start N workers performing module requests" },
	{ NULL,	"module-name F",    "use the specified module name F to load." },
	{ NULL,	"module-no-unload", "skip unload of the module after module load" },
	{ NULL,	"module-no-modver", "ignore symbol version hashes" },
	{ NULL,	"module-no-vermag", "ignore kernel version magic" },
	{ NULL,	"module-ops N",     "stop after N module bogo operations" },
	{ NULL,	NULL,		    NULL }
};

static int stress_module_supported(const char *name)
{
        if (!stress_check_capability(SHIM_CAP_SYS_MODULE)) {
                pr_inf_skip("%s stressor will be skipped, "
                        "need to be running with CAP_SYS_MODULE "
                        "rights for this stressor\n", name);
                return -1;
        }
	return 0;
}

static const stress_opt_t opts[] = {
	{ OPT_module_name,      "module-name",      TYPE_ID_STR,  0, 0, NULL },
	{ OPT_module_no_modver, "module-no-modver", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_module_no_vermag, "module-no-vermag", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_module_no_unload, "module-no-unload", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(__linux__)

enum parse_line_type {
	PARSE_COMMENT = 0,
	PARSE_EMPTY,
	PARSE_DEPMOD_MODULE,
	PARSE_INVALID,
	PARSE_EOF,
};

/* Taken from kmod.git to keep bug compatible */
static char global_module_path[PATH_MAX];

#if defined(HAVE_UNAME) &&      \
    defined(HAVE_SYS_UTSNAME_H)
static bool PURE isempty(const char *line, const size_t line_len)
{
	size_t i = 0;

	while (i < line_len) {
		const char p = line[i++];

		/* tab or space */
		if (!isblank(p))
			return false;
	}

	return true;
}

static bool PURE iscomment(const char *line, const size_t line_len)
{
	size_t i = 0;

	while (i != line_len) {
		const char p = line[i];

		i++;
		/* tab or space */
		if (isblank(p))
			continue;

		if (p == '#')
			return true;

		return false;
	}

	return false;
}

static enum parse_line_type parse_get_line_type(
	const char *line,
	const size_t line_len,
	char *module,
	const size_t module_len)
{
	int ret;
	char fmt[16];

	if (isempty(line, line_len))
		return PARSE_EMPTY;

	if (iscomment(line, line_len))
		return PARSE_COMMENT;

	(void)snprintf(fmt, sizeof(fmt), "%%%zd[^:]:", module_len - 1);

	/* should be a "kernel/foo/path.ko: .* */
	ret = sscanf(line, fmt, module);
	if (ret == 1)
		return PARSE_DEPMOD_MODULE;

	if (ret == EOF)
		return PARSE_EOF;

	errno = EINVAL;
	return PARSE_INVALID;
}
#endif

/*
 * We can surely port over some of the kmod index file stuff, but
 * that's pretty complex. Instead we just write our own simple
 * modules.dep parser.
 *
 * This reads /lib/modules/$(uname -r)/modules.dep for the module name
 * to get the module path name.
 *
 * No dependencies are loaded, we're not stressing modprobe, we're
 * stressing finit_module(). You must have your dependencies
 * loaded.
 *
 * On success returns 0 and sets module_path to the path of the
 * module you should load with finit_module.
 */
static int get_modpath_name(
	stress_args_t *args,
	const char *name,
	char *module_path,
	const size_t module_path_size)
{
#if defined(HAVE_UNAME) &&      \
    defined(HAVE_SYS_UTSNAME_H)
	struct utsname u;
	char depmod[PATH_MAX];
	FILE *fp;
	char *line = NULL;
	ssize_t line_len;
	size_t len = 0, lineno = 0;
	static const char *dirname_default_prefix = "/lib/modules";
	char module[PATH_MAX - 256];		/* used by our parser */
	char module_path_truncated[PATH_MAX];	/* truncated path */
	char module_path_basename[PATH_MAX];
	char module_short[PATH_MAX];
	enum parse_line_type parse_type;
	int ret = -1;

        if (uname(&u) < 0)
		return -1;
	(void)snprintf(depmod, sizeof(depmod), "%s/%s/modules.dep",
		dirname_default_prefix, u.release);

	fp = fopen(depmod, "r");
	if (!fp)
		goto out_close;

	while ((line_len = getline(&line, &len, fp)) != -1) {
		char *module_pathp;
		char *start_postfix;
		char *modulenamep;

		lineno++;
		parse_type = parse_get_line_type(line, (size_t)line_len, module, sizeof(module));

		switch (parse_type) {
		case PARSE_EMPTY:
		case PARSE_COMMENT:
			/* Nothing tag to free for these */
			break;
		case PARSE_EOF:
			goto out_close;
		case PARSE_DEPMOD_MODULE:
			/* truncates the "kernel/" part */
			module_pathp = strchr(module, '/');
			if (module_pathp == NULL) {
				free(line);
				line = NULL;
				break;
			}
			(void)shim_strscpy(module_path_truncated, module_pathp, sizeof(module_path_truncated));

			/* basename can modify the the original string */
			modulenamep = basename(module_path_truncated);
			(void)shim_strscpy(module_path_basename, modulenamep, sizeof(module_path_basename));

			start_postfix = strchr(module_path_basename, '.');
			if (!start_postfix) {
				free(line);
				line = NULL;
				break;
			}
			*start_postfix  = '\0';

			(void)shim_strscpy(module_short, module_path_basename, sizeof(module_short));
			if (strlen(name) != strlen(module_short)) {
				free(line);
				line = NULL;
				break;
			}
			if (strncmp(name, module_short, strlen(name)) != 0) {
				free(line);
				line = NULL;
				break;
			}
			(void)snprintf(module_path, module_path_size, "%s/%s/%s",
				 dirname_default_prefix,
				 u.release, module);
			ret = 0;
			goto out_close;
		case PARSE_INVALID:
			ret = -1;
			pr_inf("%s: invalid line in '%s' at line %zu: '%s'\n",
				args->name, depmod, lineno, line);
			goto out_close;
		}

		free(line);
		line = NULL;
	}

out_close:
	if (line)
		free(line);
	if (fp)
		(void)fclose(fp);
	return ret;
#else
	(void)args;
	(void)name;
	(void)module_path;
	(void)module_path_size;
	return -1;
#endif
}

/*
 *  stress_module
 *	stress by heavy module ops
 */
static int stress_module(stress_args_t *args)
{
	bool module_no_unload = false;
	bool module_no_vermag = false;
	bool module_no_modver = false;
	char *module_name_cli = NULL;
	char *module_name;
	static char default_module[] = "test_module";
	const char *finit_args1 = "";
	unsigned int kernel_flags = 0;
	struct stat statbuf;
	int fd, ret = EXIT_SUCCESS;

	(void)stress_get_setting("module-name", &module_name_cli);
	(void)stress_get_setting("module-no-vermag", &module_no_vermag);
	(void)stress_get_setting("module-no-modver", &module_no_modver);
	(void)stress_get_setting("module-no-unload", &module_no_unload);

	if (module_no_vermag)
		kernel_flags |= MODULE_INIT_IGNORE_VERMAGIC;
	if (module_no_modver)
		kernel_flags |= MODULE_INIT_IGNORE_MODVERSIONS;

	module_name = module_name_cli ? module_name_cli : default_module;
	ret = get_modpath_name(args, module_name, global_module_path, sizeof(global_module_path));
	if (ret < 0) {
		if (args->instance == 0) {
			if (module_name_cli) {
				pr_inf_skip("%s: could not find a module path for "
					"the specified module '%s', ensure it "
					"is enabled in your running kernel, "
					"skipping stressor\n",
					args->name, module_name);
			} else {
				pr_inf_skip("%s: could not find a module path for "
					"the default test_module '%s', perhaps "
					"CONFIG_TEST_LKM is disabled in your "
					"kernel (or alternatively use --module-name "
					"to specify module), skipping stressor\n",
					args->name, module_name);
			}
		}
		return EXIT_NO_RESOURCE;
	}

	/*
	 *  We're exercising modules, so if the open fails chalk this
	 *  up as a resource failure rather than a module test failure.
	 */
	fd = open(global_module_path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		pr_inf_skip("%s: cannot open the module file %s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, global_module_path,
			errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto out;
	}

	/*
	 *  Use fstat rather than stat to avoid TOCTOU (time-of-check,
	 *  time-of-use) race
	 */
	if (shim_fstat(fd, &statbuf) < 0) {
		if (args->instance == 0) {
			if (module_name_cli) {
				pr_inf_skip("%s: could not get fstat() on "
					"the specified module '%s', "
					"skipping stressor\n",
					args->name, global_module_path);
			} else {
				pr_inf_skip("%s: could not get fstat() on "
					"the default module '%s', "
					"skipping stressor\n",
					args->name, global_module_path);
			}
		}
		(void)close(fd);
		return EXIT_NO_RESOURCE;
	}
	if (!S_ISREG(statbuf.st_mode)) {
		if (args->instance == 0) {
			pr_inf_skip("%s: module passed is not a regular file "
				"'%s', skipping stressor\n",
				args->name, global_module_path);
		}
		(void)close(fd);
		return EXIT_NO_RESOURCE;
	}

	/*
	 * Always unload the module unless the user asked to not do it.
	 * As a sanity we try to unload it prior to loading it for
	 * the first time.
	 */
	if (!module_no_unload)
		(void)shim_delete_module(module_name, 0);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	stress_sync_start_wait(args);

	do {
		if (!stress_continue(args))
			break;

		if (shim_finit_module(fd, finit_args1, kernel_flags) == 0) {
			if (!module_no_unload)
				(void)shim_delete_module(module_name, 0);
		}

		stress_bogo_inc(args);
	} while (stress_continue(args));

out:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (fd >= 0)
		(void)close(fd);

	return ret;
}

stressor_info_t stress_module_info = {
	.stressor = stress_module,
	.class = CLASS_OS,
	.opts = opts,
	.supported = stress_module_supported,
	.help = help
};

#else
stressor_info_t stress_module_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS,
	.opts = opts,
	.supported = stress_module_supported,
	.help = help
};
#endif
