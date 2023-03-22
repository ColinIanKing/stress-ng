/*
 * Copyright (C) 2023 Luis Chamberlain <mcgrof@kernel.org>
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

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

static const stress_help_t help[] = {
	{ NULL,	"module N",	    "start N workers performing module requests" },
	{ NULL,	"module-name F",    "use the specified module name F to load." },
	{ NULL,	"module-nounload",  "skip unload of the module after module load" },
	{ NULL,	"module-sharedfd",  "use a shared file descriptor for all loads" },
	{ NULL,	"module-nomodver",  "ignore symbol version hashes" },
	{ NULL,	"module-novermag",  "ignore kernel version magic" },
	{ NULL,	"module-ops N",     "stop after N module bogo operations" },
	{ NULL,	NULL,		NULL }
};

enum parse_line_type {
	PARSE_COMMENT = 0,
	PARSE_EMPTY,
	PARSE_DEPMOD_MODULE,
	PARSE_INVALID,
	PARSE_EOF,
};

/* Taken from kmod.git to keep bug compatible */
static const char *dirname_default_prefix = "/lib/modules";
static bool module_path_found = false;
static char global_module_path[PATH_MAX];
static int global_module_fd;

static int stress_set_module_nounload(const char *opt)
{
	return stress_set_setting_true("module-nounload", opt);
}

static int stress_set_module_sharedfd(const char *opt)
{
	return stress_set_setting_true("module-sharedfd", opt);
}

static int stress_set_module_nomodver(const char *opt)
{
	return stress_set_setting_true("module-nomodver", opt);
}

static int stress_set_module_novermag(const char *opt)
{
	return stress_set_setting_true("module-novermag", opt);
}

static int stress_set_module_name(const char *name)
{
	return stress_set_setting("module-name", TYPE_ID_STR, name);
}

static bool isempty(const char *line, ssize_t linelen)
{
	ssize_t i = 0;
	char p;

	while (i < linelen) {
		p = line[i++];

		/* tab or space */
		if (!isblank(p))
			return false;
	}

	return true;
}

static bool iscomment(const char *line, ssize_t	linelen)
{
	ssize_t i = 0;
	char p;

	while (i != linelen) {
		p = line[i];
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

static enum parse_line_type parse_get_line_type(const char *line,
						ssize_t linelen,
						char *module)
{
	int ret;

	if (isempty(line, linelen))
		return PARSE_EMPTY;

	if (iscomment(line, linelen))
		return PARSE_COMMENT;

	/* should be a "kernel/foo/path.ko: .* */
	ret = sscanf(line, "%[^:]:", module);
	if (ret == 1) {
		return PARSE_DEPMOD_MODULE;
	}

	if (ret == EOF)
		return PARSE_EOF;

	errno = EINVAL;
	return PARSE_INVALID;
}

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
int get_modpath_name(const char *name, char *module_path)
{
#if defined(HAVE_UNAME) &&      \
    defined(HAVE_SYS_UTSNAME_H)
	struct utsname u;
	char depmod[PATH_MAX];
	FILE *fp;
	char *line = NULL;
	ssize_t linelen;
	size_t len = 0, lineno = 0;
	char module[PATH_MAX]; /* used by our parser */
	char module_path_truncated[PATH_MAX]; /* truncated path */
	char module_path_basename[PATH_MAX];
	char module_short[PATH_MAX];
	int ret = -1;
	enum parse_line_type parse_type;

        if (uname(&u) < 0)
		return -1;
	snprintf(depmod, sizeof(depmod), "%s/%s/modules.dep",
		 dirname_default_prefix, u.release);

	fp = fopen(depmod, "r");
	if (!fp)
		goto out_close;
	while ((linelen = getline(&line, &len, fp)) != -1) {
		lineno++;
		parse_type = parse_get_line_type(line, linelen, module);
		switch (parse_type) {
		case PARSE_EMPTY:
		case PARSE_COMMENT:
			/* Nothing tag to free for these */
			break;
		case PARSE_EOF:
			goto out_close;
		case PARSE_DEPMOD_MODULE:
			char *module_pathp;
			char *start_postfix;
			char *modulenamep;
			/* truncates the "kernel/" part */
			module_pathp = strchr(module, '/');
			if (module_pathp == NULL) {
				free(line);
				line = NULL;
				break;
			}

			memset(module_path_truncated, 0, PATH_MAX);
			strncpy(module_path_truncated, module_pathp, PATH_MAX);

			/* basename can modify the the original string */
			modulenamep = basename(module_path_truncated);
			memset(module_path_basename, 0, PATH_MAX);
			strcpy(module_path_basename, modulenamep);

			start_postfix = strchr(module_path_basename, '.');
			if (start_postfix == NULL) {
				free(line);
				line = NULL;
				break;
			}
			*start_postfix  = '\0';

			memset(module_short, 0, PATH_MAX);
			strncpy(module_short, module_path_basename, PATH_MAX);
			if (strlen(name) != strlen (module_short)) {
				free(line);
				line = NULL;
				break;
			}
			if (strncmp(name, module_short, strlen(name)) != 0) {
				free(line);
				line = NULL;
				break;
			}
			//snprintf(module_path, strlen(module_path), "%s/%s/%s",
			snprintf(module_path, PATH_MAX*2, "%s/%s/%s",
				 dirname_default_prefix,
				 u.release, module);
			ret = 0;
			goto out_close;
		case PARSE_INVALID:
			ret = -1;
			fprintf(stderr, "Invalid line %s:%zu : %s\n", depmod, lineno, line);
			goto out_close;
		}

		free(line);
		line = NULL;
	}

out_close:
	if (line)
		free(line);
	fclose(fp);
	return ret;
#else
	return -1;
#endif
}


/*
 *  stress_module
 *	stress by heavy module ops
 */
static int stress_module(const stress_args_t *args)
{
	bool module_nounload = false;
	bool module_sharedfd = false;
	bool ignore_vermagic = false;
	bool ignore_modversions = false;
	char *module_name_cli = NULL;
	char module_path[PATH_MAX];
	char *module_name;
	char *default_module = "test_module";
	const char *finit_args1 = "";
	unsigned int kernel_flags = 0;
	struct stat statbuf;
	int ret = EXIT_SUCCESS;

	(void)stress_get_setting("module-name", &module_name_cli);
	(void)stress_get_setting("module-novermag", &ignore_vermagic);
	(void)stress_get_setting("module-nomodver", &ignore_modversions);
	(void)stress_get_setting("module-nounload", &module_nounload);
	(void)stress_get_setting("module-sharedfd", &module_sharedfd);

	if (geteuid() != 0) {
		if (args->instance == 0)
			pr_inf("%s: need root privilege to run "
				"this stressor\n", args->name);
		/* Not strictly a test failure */
		return EXIT_SUCCESS;
	}

	if (ignore_vermagic)
		kernel_flags |= MODULE_INIT_IGNORE_VERMAGIC;
	if (ignore_modversions)
		kernel_flags |= MODULE_INIT_IGNORE_MODVERSIONS;

	if (module_name_cli)
		module_name = module_name_cli;
	else
		module_name = default_module;

	/*
	 * We're not stressing the modules.dep --> module path lookup,
	 * just the finit_module() calls and so only do the lookup once.
	 */
	if (args->instance != 0) {
		if (!module_path_found)
			return EXIT_SUCCESS;
	} else {
		ret = get_modpath_name(module_name, module_path);
		if (ret != 0) {
			if (module_name == default_module)
				pr_inf_skip("%s: could not find a module path for the default test_module '%s', perhaps CONFIG_TEST_LKM is disabled in your kernel, skipping stressor\n",
					    args->name, module_name);
			else
				pr_inf_skip("%s: could not find a module path for module you specified '%s', ensure it is enabld in your running kernel, skipping stressor\n",
					    args->name, module_name);
			return EXIT_NO_RESOURCE;
		}
		if (stat(module_path, &statbuf) < 0) {
			if (args->instance == 0) {
				if (module_path != default_module)
					pr_inf_skip("%s: could not get stat() on the module you specified '%s', skipping stressor\n",
						    args->name, module_path);
				else
					pr_inf_skip("%s: could not get stat() on the default module '%s', skipping stressor (XXX implement utsname path completion)\n",
						    args->name, module_path);
			}
			return EXIT_NO_RESOURCE;
		}
		if (!S_ISREG(statbuf.st_mode)) {
			pr_inf_skip("%s: module passed is not a regular file '%s', skipping stressor\n",
				    args->name, module_path);
			return EXIT_NO_RESOURCE;
		}

		memcpy(global_module_path, module_path, PATH_MAX);

		if (module_sharedfd) {
			global_module_fd = open(global_module_path, O_RDONLY | O_CLOEXEC);

			if (global_module_fd < 0) {
				/* Check if we hit the open file limit */
				if ((errno == EMFILE) || (errno == ENFILE)) {
					ret = EXIT_NO_RESOURCE;
					goto out;
				}
				pr_inf_skip("%s: unexpected error while opening module file %s, skipping stressor\n",
					    args->name, global_module_path);
				ret = EXIT_NO_RESOURCE;
				goto out;
			}
		}
		module_path_found = true;
	}

	/*
	 * Always unload the module unless the user asked to not do it.
	 * As a sanity we try to unload it prior to loading it for
	 * the first time.
	 */
	if (!module_nounload)
		shim_delete_module(module_name, 0);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int ret;
		int fd;

		if (!keep_stressing(args))
			goto out;

		if (module_sharedfd)
			fd = global_module_fd;
		else {
			fd = open(global_module_path, O_RDONLY | O_CLOEXEC);

			if (fd < 0) {
				/* Check if we hit the open file limit */
				if ((errno == EMFILE) || (errno == ENFILE)) {
					ret = EXIT_NO_RESOURCE;
					goto out;
				}
				/* Ignore other errors */
				continue;
			}
		}

		ret = shim_finit_module(fd, finit_args1, kernel_flags);
		if (ret == 0) {
			if (!module_nounload)
				shim_delete_module(module_name, 0);
		}

		if (!module_sharedfd)
			(void)close(fd);

	} while (keep_stressing(args));

out:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (module_sharedfd && args->instance == 0)
		close(global_module_fd);

	return ret;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_module_name,	stress_set_module_name},
	{ OPT_module_nomodver,	stress_set_module_nomodver },
	{ OPT_module_novermag,	stress_set_module_novermag },
	{ OPT_module_nounload,	stress_set_module_nounload },
	{ OPT_module_sharedfd,	stress_set_module_sharedfd},
	{ 0,					NULL }
};

stressor_info_t stress_module_info = {
	.stressor = stress_module,
	.class = CLASS_OS | CLASS_FILESYSTEM | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
