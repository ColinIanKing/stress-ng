/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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
#include <sys/statvfs.h>

#define	STRESS_FILENAME_PROBE	(0)	/* Default */
#define STRESS_FILENAME_POSIX	(1)	/* POSIX 2008.1 */
#define STRESS_FILENAME_EXT	(2)	/* EXT* filesystems */

typedef struct {
	int opt;
	const char *opt_text;
} filename_opts_t;

static const filename_opts_t filename_opts[] = {
	{ STRESS_FILENAME_PROBE,	"probe" },
	{ STRESS_FILENAME_POSIX,	"posix" },
	{ STRESS_FILENAME_EXT,		"ext" },
	{ -1,				NULL }
};

/* Allowed filename characters */
static char allowed[256];
static int filename_opt = STRESS_FILENAME_PROBE;

/*
 * The Open Group Base Specifications Issue 7
 * POSIX.1-2008, 3.278 Portable Filename Character Set
 */
static char posix_allowed[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz"
	"0123456789._-";

int stress_filename_opts(const char *opt)
{
	size_t i;

	for (i = 0; filename_opts[i].opt_text; i++) {
		if (!strcmp(opt, filename_opts[i].opt_text)) {
			filename_opt = filename_opts[i].opt;
			return 0;
		}
	}
	(void)fprintf(stderr, "filename-opt option '%s' not known, options are:", opt);
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
		int fd;

		if ((i == 0) || (i == '/'))
			continue;
		*ptr = i;
		*(ptr + 1) = 'X';
		*(ptr + 2) = '\0';

		if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
			/* We only expect EINVAL on bad filenames */
			if (errno != EINVAL) {
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
int stress_filename (const args_t *args)
{
	int ret, rc = EXIT_FAILURE;
	size_t sz_left, sz_max;
	char dirname[PATH_MAX];
	char filename[PATH_MAX];
	char *ptr;
	struct statvfs buf;
	size_t i, chars_allowed = 0, sz;

	stress_temp_dir_args(args, dirname, sizeof(dirname));
	if (mkdir(dirname, S_IRWXU) < 0) {
		if (errno != EEXIST) {
			pr_fail_err("mkdir");
			return EXIT_FAILURE;
		}
	}

	if (statvfs(dirname, &buf) < 0) {
		pr_fail_err("statvfs");
		goto tidy_dir;
	}
	if (args->instance == 0)
		pr_dbg("%s: maximum file size: %lu bytes\n",
			args->name, (long unsigned) buf.f_namemax);

	strncpy(filename, dirname, sizeof(filename) - 1);
	ptr = filename + strlen(dirname);
	*(ptr++) = '/';
	*(ptr) = '\0';
	sz_left = sizeof(filename) - (ptr - filename);
	sz_max = (size_t)buf.f_namemax;

	if (sz_left >= PATH_MAX) {
		pr_fail("%s: max file name larger than PATH_MAX\n", args->name);
		goto tidy_dir;
	}

	switch (filename_opt) {
	case STRESS_FILENAME_POSIX:
		strncpy(allowed, posix_allowed, sizeof(allowed));
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

	i = 0;
	sz = 1;
	do {
		char ch = allowed[i];
		size_t rnd_sz = 1 + (mwc32() % sz_max);

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

	rc = EXIT_SUCCESS;

tidy_dir:
	(void)rmdir(dirname);

	return rc;
}
