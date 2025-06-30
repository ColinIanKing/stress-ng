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
#include "core-builtin.h"
#include "core-killpid.h"
#include "core-out-of-memory.h"

#if defined(HAVE_SYS_STATVFS_H)
#include <sys/statvfs.h>
#endif

#define	STRESS_FILENAME_PROBE	(0)	/* Default */
#define STRESS_FILENAME_POSIX	(1)	/* POSIX 2008.1 */
#define STRESS_FILENAME_EXT	(2)	/* EXT* filesystems */

/* mapping of opts text to STRESS_FILE_NAME_* values */
static const char * const filename_opts[] = {
	"probe",	/* STRESS_FILENAME_PROBE */
	"posix",	/* STRESS_FILENAME_POSIX */
	"ext",		/* STRESS_FILENAME_EXT */
};

static const stress_help_t help[] = {
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
static const char posix_allowed[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz"
	"0123456789._-";

/*
 *  stress_filename_probe_length()
 *	see if advertised path size is ok, shorten if
 *	necessary
 */
static int stress_filename_probe_length(
	stress_args_t *args,
	char *filename,
	char *ptr,
	size_t *sz_max)
{
	size_t i;
	size_t max = 0;

	for (i = 0; i < *sz_max; i++) {
		int fd;

		*(ptr + i) = 'a';
		*(ptr + i + 1) = '\0';

		if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
			if (errno == ENOTSUP)
				break;
			if (errno == ENAMETOOLONG)
				break;
			pr_err("%s: creat() failed when probing "
				"for filename length, "
				"errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			*sz_max = 0;
			return -1;
		}
		(void)close(fd);
		if (shim_unlink(filename)) {
			pr_err("%s: unlink() failed when probing "
				"for filename length, "
				"errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			*sz_max = 0;
			return -1;
		}
		max = i;
	}
	*sz_max = max + 1;
	return 0;
}


/*
 *  stress_filename_probe()
 *	determine allowed filename chars by probing
 */
static int stress_filename_probe(
	stress_args_t *args,
	char *filename,
	char *ptr,
	size_t sz_max,
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
		for (k = 0; k < sz_max; k++)
			*(ptr + k) = (char)i;
		*(ptr + k) = '\0';

		if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
			/*
			 *  We only expect EINVAL on bad filenames,
			 *  and WSL on Windows 10 can return ENOENT
			 */
			if ((errno != EINVAL) &&
			    (errno != ENOENT) &&
			    ((errno == ENAMETOOLONG) || (errno == ENOTSUP)) &&
			    (errno != EILSEQ)) {
				pr_err("%s: creat() failed when probing "
					"for allowed filename characters, "
					"errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				pr_inf("%s: perhaps retry and use "
					"--filename-opts posix\n", args->name);
				*chars_allowed = 0;
				return -errno;
			}
		} else {
			(void)close(fd);
			if (shim_unlink(filename)) {
				pr_err("%s: unlink() failed when probing "
					"for allowed filename characters, "
					"errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				*chars_allowed = 0;
				return -errno;
			}
			allowed[j] = (char)i;
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
		allowed[j] = (char)i;
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
static void stress_filename_tidy(
	stress_args_t *args,
	const char *path,
	int *rc
)
{
	DIR *dir;

	dir = opendir(path);
	if (dir) {
		const struct dirent *d;

		while ((d = readdir(dir)) != NULL) {
			char filename[PATH_MAX];

			if (stress_is_dot_filename(d->d_name))
				continue;
			(void)stress_mk_filename(filename, sizeof(filename),
				path, d->d_name);
			if (shim_unlink(filename)) {
				pr_fail("%s: unlink() failed when tidying, "
					"errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				*rc = EXIT_FAILURE;
			}
		}
		(void)closedir(dir);
	}
	(void)shim_rmdir(path);
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
		const size_t j = (size_t)stress_mwc32modn(chars_allowed);

		filename[i] = allowed[j];
	}
	if (*filename == '.')
		*filename = '_';
	filename[i] = '\0';
}

/*
 *  stress_filename_generate_random_utf8()
 *	generate a utf8 filename, may be legal or not
 *	for the filename charset being supported
 */
static void stress_filename_generate_random_utf8(
	char *filename,
	const size_t sz_max)
{
	size_t i = 0, j = 0;

	while (i < sz_max) {
		const size_t residual = STRESS_MINIMUM(sz_max - i, 4);
		const size_t len = stress_mwc8modn(residual) + 1;

		switch (len) {
		default:
		case 1:
			filename[i++] = stress_mwc8modn(127) + 1;
			break;
		case 2:
			filename[i++] = 0xc0 | (stress_mwc8() & 0x1f);
			j = i;
			filename[i++] = 0x80 | (stress_mwc8() & 0x3f);
			break;
		case 3:
			filename[i++] = 0xe0 | (stress_mwc8() & 0x0f);
			filename[i++] = 0x80 | (stress_mwc8() & 0x3f);
			j = i;
			filename[i++] = 0x80 | (stress_mwc8() & 0x3f);
			break;
		case 4:
			filename[i++] = 0xf0 | (stress_mwc8() & 0x07);
			filename[i++] = 0x80 | (stress_mwc8() & 0x3f);
			filename[i++] = 0x80 | (stress_mwc8() & 0x3f);
			j = i;
			filename[i++] = 0x80 | (stress_mwc8() & 0x3f);
			break;
		}
	}
	filename[i] = '\0';
	/*
	 *  occassionally truncate valid utf8 filename to create
	 *  invalid utf8 strings, c.f.:
	 *  https://sourceware.org/pipermail/cygwin/2024-September/256451.html
	 */
	if (j && stress_mwc8() < 16)
		filename[j] = '\0';

}

/*
 *  stress_filename_test()
 *	create a file, and check if it fails.
 *	should_pass = true - create must pass
 *	should_pass = false - expect it to fail (name too long)
 */
static void stress_filename_test(
	stress_args_t *args,
	const char *filename,
	const size_t sz_max,
	const bool should_pass,
	const pid_t pid,
	int *rc)
{
	int fd;
	int ret;
	struct stat buf;

	/* exercise dcache lookup of non-existent filename */
	ret = shim_stat(filename, &buf);
	if (ret == 0) {
		pr_fail("%s: stat succeeded on non-existent file\n",
			args->name);
		*rc = EXIT_FAILURE;
	}

	if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
		if (errno == ENOTSUP)
			return;
		if ((!should_pass) && (errno == ENAMETOOLONG))
			return;

		pr_fail("%s: creat() failed on file of length "
			"%zu bytes, errno=%d (%s)\n",
			args->name, sz_max, errno, strerror(errno));
		*rc = EXIT_FAILURE;
	} else {
		stress_read_fdinfo(pid, fd);
		(void)close(fd);

		/* exercise dcache lookup of existent filename */
		VOID_RET(int, shim_stat(filename, &buf));

		if (shim_unlink(filename)) {
			pr_fail("%s: unlink() failed on file of length "
				"%zu bytes, errno=%d (%s)\n",
				args->name, sz_max, errno, strerror(errno));
			*rc = EXIT_FAILURE;
			return;
		}
	}

	/* exercise dcache lookup of non-existent filename */
	ret = shim_stat(filename, &buf);
	if (ret == 0) {
		pr_fail("%s: stat succeeded on non-existent unlinked file\n",
			args->name);
		*rc = EXIT_FAILURE;
	}
}

/*
 *  stress_filename_test_utf8()
 *      exercise utf8 filename, may or may not fail
 */
static void stress_filename_test_utf8(
	stress_args_t *args,
	const char *filename,
	const size_t sz_max,
	const pid_t pid,
	int *rc)
{
	int fd;
	int ret;
	struct stat buf;

	/* exercise dcache lookup of non-existent filename */
	VOID_RET(int, shim_stat(filename, &buf));

	if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0)
		return;

	stress_read_fdinfo(pid, fd);
	(void)close(fd);

	/* exercise dcache lookup of existent filename */
	VOID_RET(int, shim_stat(filename, &buf));

	if (shim_unlink(filename)) {
		pr_fail("%s: unlink() failed on file of length "
			"%zu bytes, errno=%d (%s)\n",
			args->name, sz_max, errno, strerror(errno));
		*rc = EXIT_FAILURE;
		return;
	}

	/* exercise dcache lookup of non-existent filename */
	ret = shim_stat(filename, &buf);
	if (ret == 0) {
		pr_fail("%s: stat succeeded on non-existent unlinked file\n",
			args->name);
		*rc = EXIT_FAILURE;
	}
}

/*
 *  stress_filename()
 *	stress filename sizes etc
 */
static int stress_filename(stress_args_t *args)
{
	int ret, rc = EXIT_SUCCESS;
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

	(void)stress_get_setting("filename-opts", &filename_opt);

	stress_temp_dir_args(args, pathname, sizeof(pathname));
	if (mkdir(pathname, S_IRWXU) < 0) {
		if (errno != EEXIST) {
			pr_fail("%s: mkdir %s failed, errno=%d (%s)\n",
				args->name, pathname, errno, strerror(errno));
			return EXIT_FAILURE;
		}
	}

#if defined(HAVE_SYS_STATVFS_H)
	(void)shim_memset(&buf, 0, sizeof(buf));
	if (statvfs(pathname, &buf) < 0) {
		pr_fail("%s: statvfs %s failed, errno=%d (%s)%s\n",
			args->name, pathname, errno, strerror(errno),
			stress_get_fs_type(pathname));
		goto tidy_dir;
	}
#else
	UNEXPECTED
#endif

	(void)shim_strscpy(filename, pathname, sizeof(filename) - 1);
	ptr = filename + strlen(pathname);
	*(ptr++) = '/';
	*(ptr) = '\0';
	sz_left = sizeof(filename) - (size_t)(ptr - filename);

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
		pr_fail("%s: max file name larger than PATH_MAX\n",
			args->name);
		goto tidy_dir;
	}

	if (stress_filename_probe_length(args, filename, ptr, &sz_max) < 0) {
		pr_fail("%s: failed to determine maximum filename length%s\n",
			args->name, stress_get_fs_type(pathname));
		goto tidy_dir;
	}

	switch (filename_opt) {
	case STRESS_FILENAME_POSIX:
		(void)shim_strscpy(allowed, posix_allowed, sizeof(allowed));
		chars_allowed = strlen(allowed);
		break;
	case STRESS_FILENAME_EXT:
		stress_filename_ext(&chars_allowed);
		break;
	case STRESS_FILENAME_PROBE:
	default:
		ret = stress_filename_probe(args, filename, ptr, sz_max, &chars_allowed);
		if (ret < 0) {
			rc = stress_exit_status(-ret);
			goto tidy_dir;
		}
		break;
	}

	if (stress_instance_zero(args))
		pr_dbg("%s: filesystem allows %zu unique "
			"characters in a %zu character long filename\n",
			args->name, chars_allowed, sz_max);

	if (chars_allowed == 0) {
		pr_fail("%s: cannot determine allowed characters "
			"in a filename\n", args->name);
		goto tidy_dir;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	if (UNLIKELY(!stress_continue_flag())) {
		/* Time to die */
		rc = EXIT_SUCCESS;
		goto tidy_dir;
	}
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args))) {
			rc = EXIT_SUCCESS;
			goto tidy_dir;
		}
		pr_err("%s: fork failed, errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status;

		/* Parent, wait for child */
		ret = shim_waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid() on PID %" PRIdMAX" failed, errno=%d (%s)\n",
					args->name, (intmax_t)pid, errno, strerror(errno));
			stress_force_killed_bogo(args);
			(void)stress_kill_pid_wait(pid, NULL);
		} else if (WIFSIGNALED(status)) {
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				if (g_opt_flags & OPT_FLAGS_OOMABLE) {
					stress_log_system_mem_info();
					pr_dbg("%s: assuming killed by OOM "
						"killer, bailing out "
						"(instance %d)\n",
						args->name, args->instance);
					_exit(0);
				} else {
					stress_log_system_mem_info();
					pr_dbg("%s: assuming killed by OOM "
						"killer, restarting again "
						"(instance %d)\n",
						args->name, args->instance);
					goto again;
				}
			}
		} else if (WIFEXITED(status)) {
			rc = WEXITSTATUS(status);
		}
	} else {
		/* Child, wrapped to catch OOMs */
		const pid_t mypid = getpid();

		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		/* Make sure this is killable by OOM killer */
		stress_set_oom_adjustment(args, true);

		i = 0;
		sz = 1;
		do {
			const char ch = allowed[i];
			const size_t rnd_sz = 1 + stress_mwc32modn((uint32_t)sz_max);

			i++;
			if (i >= chars_allowed)
				i = 0;

			/* Should succeed */
			stress_filename_generate(ptr, 1, ch);
			stress_filename_test(args, filename, 1, true, mypid, &rc);
			if (UNLIKELY(!stress_continue(args)))
				break;
			stress_filename_generate_random(ptr, 1, chars_allowed);
			stress_filename_test(args, filename, 1, true, mypid, &rc);
			if (UNLIKELY(!stress_continue(args)))
				break;

			/* Should succeed */
			stress_filename_generate(ptr, sz_max, ch);
			stress_filename_test(args, filename, sz_max, true, mypid, &rc);
			if (UNLIKELY(!stress_continue(args)))
				break;
			stress_filename_generate_random(ptr, sz_max, chars_allowed);
			stress_filename_test(args, filename, sz_max, true, mypid, &rc);
			if (UNLIKELY(!stress_continue(args)))
				break;

			/* Should succeed */
			stress_filename_generate(ptr, sz_max - 1, ch);
			stress_filename_test(args, filename, sz_max - 1, true, mypid, &rc);
			if (UNLIKELY(!stress_continue(args)))
				break;
			stress_filename_generate_random(ptr, sz_max - 1, chars_allowed);
			stress_filename_test(args, filename, sz_max - 1, true, mypid, &rc);
			if (UNLIKELY(!stress_continue(args)))
				break;

			/* Should fail */
			stress_filename_generate(ptr, sz_max + 1, ch);
			stress_filename_test(args, filename, sz_max + 1, false, mypid, &rc);
			if (UNLIKELY(!stress_continue(args)))
				break;
			stress_filename_generate_random(ptr, sz_max + 1, chars_allowed);
			stress_filename_test(args, filename, sz_max + 1, false, mypid, &rc);
			if (UNLIKELY(!stress_continue(args)))
				break;

			/* Should succeed */
			stress_filename_generate(ptr, sz, ch);
			stress_filename_test(args, filename, sz, true, mypid, &rc);
			if (UNLIKELY(!stress_continue(args)))
				break;
			stress_filename_generate_random(ptr, sz, chars_allowed);
			stress_filename_test(args, filename, sz, true, mypid, &rc);
			if (UNLIKELY(!stress_continue(args)))
				break;

			/* Should succeed */
			stress_filename_generate(ptr, rnd_sz, ch);
			stress_filename_test(args, filename, rnd_sz, true, mypid, &rc);
			if (UNLIKELY(!stress_continue(args)))
				break;
			stress_filename_generate_random(ptr, rnd_sz, chars_allowed);
			stress_filename_test(args, filename, rnd_sz, true, mypid, &rc);
			if (UNLIKELY(!stress_continue(args)))
				break;

			/* May succeed or fail */
			stress_filename_generate_random_utf8(ptr, sz_max - 1);
			stress_filename_test_utf8(args, filename, sz_max - 1, pid, &rc);
			if (UNLIKELY(!stress_continue(args)))
				break;

			if (filename_opt == STRESS_FILENAME_EXT) {
				stress_filename_generate_random_utf8(ptr, rnd_sz);
				stress_filename_test_utf8(args, filename, rnd_sz, pid, &rc);
				if (UNLIKELY(!stress_continue(args)))
					break;
			}

#if defined(HAVE_PATHCONF)
#if defined(_PC_NAME_MAX)
			VOID_RET(long int, pathconf(pathname, _PC_NAME_MAX));
#endif
#if defined(_PC_PATH_MAX)
			VOID_RET(long int, pathconf(pathname, _PC_PATH_MAX));
#endif
#if defined(_PC_NO_TRUNC)
			VOID_RET(long int, pathconf(pathname, _PC_NO_TRUNC));
#endif
#endif

			sz++;
			if (sz > sz_max)
				sz = 1;
			stress_bogo_inc(args);
		} while (stress_continue(args));
		_exit(rc);
	}

tidy_dir:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_filename_tidy(args, pathname, &rc);

	return rc;
}

static const char *stress_filename_opts(const size_t i)
{
	return (i < SIZEOF_ARRAY(filename_opts)) ? filename_opts[i] : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_filename_opts, "filename-opts", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_filename_opts },
	END_OPT,
};

const stressor_info_t stress_filename_info = {
	.stressor = stress_filename,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
