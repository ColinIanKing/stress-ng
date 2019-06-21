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

#define	STRESS_FILENAME_PROBE	(0)	/* Default */
#define STRESS_FILENAME_POSIX	(1)	/* POSIX 2008.1 */
#define STRESS_FILENAME_EXT	(2)	/* EXT* filesystems */

typedef struct {
	const uint8_t opt;
	const char *opt_text;
} filename_opts_t;

static const filename_opts_t filename_opts[] = {
	{ STRESS_FILENAME_PROBE,	"probe" },
	{ STRESS_FILENAME_POSIX,	"posix" },
	{ STRESS_FILENAME_EXT,		"ext" },
	{ -1,				NULL }
};

static const help_t help[] = {
	{ NULL,	"filename N",		"start N workers exercising filenames" },
	{ NULL,	"filename-ops N",	"stop after N filename bogo operations" },
	{ NULL,	"filename-opts opt",	"specify allowed filename options" },
	{ NULL,	NULL,			NULL }
};

/* Allowed filename characters */
static char allowed[256];

/*
 * The Open Group Base Specifications Issue 7
 * POSIX.1-2008, 3.278 Portable Filename Character Set
 */
static char posix_allowed[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz"
	"0123456789._-";

static int stress_set_filename_opts(const char *opt)
{
	size_t i;

	for (i = 0; filename_opts[i].opt_text; i++) {
		if (!strcmp(opt, filename_opts[i].opt_text)) {
			uint8_t filename_opt = filename_opts[i].opt;
			set_setting("filename-opts", TYPE_ID_UINT8, &filename_opt);
			return 0;
		}
	}
	(void)fprintf(stderr, "filename-opts option '%s' not known, options are:", opt);
	for (i = 0; filename_opts[i].opt_text; i++)
		(void)fprintf(stderr, "%s %s",
			i == 0 ? "" : ",", filename_opts[i].opt_text);
	(void)fprintf(stderr, "\n");
	return -1;
}

/*
 *  stress_filename_probe()
 *	determine allowed filename chars by probing
 */
static int stress_filename_probe(
	const args_t *args,
	char *filename,
	char *ptr,
	size_t *chars_allowed)
{
	size_t i, j;

	/*
	 *  Determine allowed char set for filenames
	 */
	for (j = 0, i = 0; i < 256; i++) {
		size_t k;
		int fd;

		if ((i == 0) || (i == '/'))
			continue;
#if defined(__APPLE__)
		if (i == ':')
			continue;
#endif
		/*
		 *  Some systems such as Windows need long file
		 *  names of around 64 chars with invalid probe
		 *  chars to be able to be detect for bad chars.
		 *  Not sure why that is.
		 */
		for (k = 0; k < 64; k++)
			*(ptr + k) = i;
		*(ptr + k) = '\0';

		if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
			/*
			 *  We only expect EINVAL on bad filenames,
			 *  and WSL on Windows 10 can return ENOENT
			 */
			if ((errno != EINVAL) && (errno != ENOENT)) {
				pr_err("%s: creat() failed when probing "
					"for allowed filename characters, "
					"errno = %d (%s)\n",
					args->name, errno, strerror(errno));
				pr_inf("%s: perhaps retry and use "
					"--filename-opts posix\n", args->name);
				*chars_allowed = 0;
				return -errno;
			}
		} else {
			(void)close(fd);
			(void)unlink(filename);
			allowed[j] = i;
			j++;
		}
	}
	*chars_allowed = j;

	return 0;
}

/*
 *  stress_filename_ext()
 *	determine allowed for ext* filesystems
 */
static void stress_filename_ext(size_t *chars_allowed)
{
	size_t i, j;

	for (j = 0, i = 0; i < 256; i++) {
		if ((i == 0) || (i == '/'))
			continue;
		allowed[j] = i;
		j++;
	}
	*chars_allowed = j;
}

/*
 *  stress_filename_generate()
 *	generate a filename of length sz_max
 */
static void stress_filename_generate(
	char *filename,
	const size_t sz_max,
	const char ch)
{
	size_t i;

	for (i = 0; i < sz_max; i++) {
		filename[i] = ch;
	}
	if (*filename == '.')
		*filename = '_';

	filename[i] = '\0';
}

/*
 *  stress_filename_tidy()
 *	clean up residual files
 */
static void stress_filename_tidy(const char *path)
{
	DIR *dir;

	dir = opendir(path);
	if (dir) {
		struct dirent *d;

		while ((d = readdir(dir)) != NULL) {
			char filename[PATH_MAX];

			if (stress_is_dot_filename(d->d_name))
				continue;
			(void)snprintf(filename, sizeof(filename),
				"%s/%s", path, d->d_name);
			(void)unlink(filename);
		}
		(void)closedir(dir);
	}
	(void)rmdir(path);
}

/*
 *  stress_filename_generate_random()
 *	generate a filename of length sz_max with
 *	random selection from possible char set
 */
static void stress_filename_generate_random(
	char *filename,
	const size_t sz_max,
	const size_t chars_allowed)
{
	size_t i;

	for (i = 0; i < sz_max; i++) {
		const int j = mwc32() % chars_allowed;
		filename[i] = allowed[j];
	}
	if (*filename == '.')
		*filename = '_';
	filename[i] = '\0';
}

/*
 *  stress_filename_test()
 *	create a file, and check if it fails.
 *	should_pass = true - create must pass
 *	should_pass = false - expect it to fail (name too long)
 */
static void stress_filename_test(
	const args_t *args,
	const char *filename,
	const size_t sz_max,
	const bool should_pass)
{
	int fd;

	if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
		if ((!should_pass) && (errno == ENAMETOOLONG))
			return;

		pr_fail("%s: open failed on file of length "
			"%zu bytes, errno=%d (%s)\n",
			args->name, sz_max, errno, strerror(errno));
	} else {
		(void)close(fd);
		(void)unlink(filename);
	}
}

/*
 *  stress_filename()
 *	stress filename sizes etc
 */
static int stress_filename(const args_t *args)
{
	int ret, rc = EXIT_FAILURE;
	size_t sz_left, sz_max;
	char pathname[PATH_MAX - 256];
	char filename[PATH_MAX];
	char *ptr;
#if defined(HAVE_SYS_STATVFS_H)
	struct statvfs buf;
#endif
	pid_t pid;
	size_t i, chars_allowed = 0, sz;
#if defined(__APPLE__)
	uint8_t filename_opt = STRESS_FILENAME_POSIX;
#else
	uint8_t filename_opt = STRESS_FILENAME_PROBE;
#endif

	(void)get_setting("filename-opts", &filename_opt);

	stress_temp_dir_args(args, pathname, sizeof(pathname));
	if (mkdir(pathname, S_IRWXU) < 0) {
		if (errno != EEXIST) {
			pr_fail_err("mkdir");
			return EXIT_FAILURE;
		}
	}

#if defined(HAVE_SYS_STATVFS_H)
	if (statvfs(pathname, &buf) < 0) {
		pr_fail_err("statvfs");
		goto tidy_dir;
	}

	if (args->instance == 0)
		pr_dbg("%s: maximum file size: %lu bytes\n",
			args->name, (long unsigned) buf.f_namemax);
#endif

	(void)shim_strlcpy(filename, pathname, sizeof(filename) - 1);
	ptr = filename + strlen(pathname);
	*(ptr++) = '/';
	*(ptr) = '\0';
	sz_left = sizeof(filename) - (ptr - filename);

#if defined(HAVE_SYS_STATVFS_H)
	sz_max = (size_t)buf.f_namemax;
#else
	sz_max = 256;
#endif

	/* Some BSD systems return zero for sz_max */
	if (sz_max == 0)
		sz_max = 128;
	if (sz_max > PATH_MAX)
		sz_max = PATH_MAX;

	if (sz_left >= PATH_MAX) {
		pr_fail("%s: max file name larger than PATH_MAX\n", args->name);
		goto tidy_dir;
	}

	switch (filename_opt) {
	case STRESS_FILENAME_POSIX:
		(void)shim_strlcpy(allowed, posix_allowed, sizeof(allowed));
		chars_allowed = strlen(allowed);
		break;
	case STRESS_FILENAME_EXT:
		stress_filename_ext(&chars_allowed);
		break;
	case STRESS_FILENAME_PROBE:
	default:
		ret = stress_filename_probe(args, filename, ptr, &chars_allowed);
		if (ret < 0) {
			rc = exit_status(-ret);
			goto tidy_dir;
		}
		break;
	}

	if (args->instance == 0)
		pr_dbg("%s: filesystem allows %zu unique "
			"characters in a filename\n",
			args->name, chars_allowed);

	if (chars_allowed == 0) {
		pr_fail("%s: cannot determine allowed characters "
			"in a filename\n", args->name);
		goto tidy_dir;
	}

again:
	if (!g_keep_stressing_flag) {
		/* Time to die */
		rc = EXIT_SUCCESS;
		goto tidy_dir;
	}
	pid = fork();
	if (pid < 0) {
		if ((errno == EAGAIN) || (errno == ENOMEM))
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status;

		(void)setpgid(pid, g_pgrp);
		/* Parent, wait for child */
		ret = shim_waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)shim_waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				if (g_opt_flags & OPT_FLAGS_OOMABLE) {
					log_system_mem_info();
					pr_dbg("%s: assuming killed by OOM "
						"killer, bailing out "
						"(instance %d)\n",
						args->name, args->instance);
					_exit(0);
				} else {
					log_system_mem_info();
					pr_dbg("%s: assuming killed by OOM "
						"killer, restarting again "
						"(instance %d)\n",
						args->name, args->instance);
					goto again;
				}
			}
		}
	} else if (pid == 0) {
		/* Child, wrapped to catch OOMs */

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(args->name, true);

		i = 0;
		sz = 1;
		do {
			const char ch = allowed[i];
			const size_t rnd_sz = 1 + (mwc32() % sz_max);

			i++;
			if (i >= chars_allowed)
				i = 0;

			/* Should succeed */
			stress_filename_generate(ptr, 1, ch);
			stress_filename_test(args, filename, 1, true);
			stress_filename_generate_random(ptr, 1, chars_allowed);
			stress_filename_test(args, filename, 1, true);

			/* Should succeed */
			stress_filename_generate(ptr, sz_max, ch);
			stress_filename_test(args, filename, sz_max, true);
			stress_filename_generate_random(ptr, sz_max, chars_allowed);
			stress_filename_test(args, filename, sz_max, true);

			/* Should succeed */
			stress_filename_generate(ptr, sz_max - 1, ch);
			stress_filename_test(args, filename, sz_max - 1, true);
			stress_filename_generate_random(ptr, sz_max - 1, chars_allowed);
			stress_filename_test(args, filename, sz_max - 1, true);

			/* Should fail */
			stress_filename_generate(ptr, sz_max + 1, ch);
			stress_filename_test(args, filename, sz_max + 1, false);
			stress_filename_generate_random(ptr, sz_max + 1, chars_allowed);
			stress_filename_test(args, filename, sz_max + 1, false);

			/* Should succeed */
			stress_filename_generate(ptr, sz, ch);
			stress_filename_test(args, filename, sz, true);
			stress_filename_generate_random(ptr, sz, chars_allowed);
			stress_filename_test(args, filename, sz, true);

			/* Should succeed */
			stress_filename_generate(ptr, rnd_sz, ch);
			stress_filename_test(args, filename, rnd_sz, true);
			stress_filename_generate_random(ptr, rnd_sz, chars_allowed);
			stress_filename_test(args, filename, rnd_sz, true);

			sz++;
			if (sz > sz_max)
				sz = 1;
			inc_counter(args);
		} while (keep_stressing());
		_exit(EXIT_SUCCESS);
	}
	rc = EXIT_SUCCESS;

tidy_dir:
	(void)stress_filename_tidy(pathname);

	return rc;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_filename_opts,	stress_set_filename_opts },
	{ 0,			NULL }
};

stressor_info_t stress_filename_info = {
	.stressor = stress_filename,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
