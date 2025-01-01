/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
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
#include "core-job.h"

#include <ctype.h>

#define MAX_ARGS	(64)
#define RUN_SEQUENTIAL	(0x01)
#define RUN_PARALLEL	(0x02)

#define ISBLANK(ch)	isblank((int)(ch))

/*
 *  stress_str_chop()
 *	chop off end of line that matches char ch
 */
static inline void stress_str_chop(char *str, const char ch)
{
	char *ptr = strchr(str, ch);

	if (ptr)
		*ptr = '\0';
}

/*
 *  stress_parse_run()
 *	parse the special job file "run" command
 *	that informs stress-ng to run the job file
 *	stressors sequentially or in parallel
 */
static int stress_parse_run(
	const char *jobfile,
	int argc,
	char **argv,
	uint32_t *flag)
{
	if (argc < 3)
		return 0;
	if (strcmp(argv[1], "run"))
		return 0;

	if (!strcmp(argv[2], "sequential") ||
	    !strcmp(argv[2], "sequentially") ||
	    !strcmp(argv[2], "seq")) {
		if (*flag & RUN_PARALLEL)
			goto err;
		*flag |= RUN_SEQUENTIAL;
		g_opt_flags |= OPT_FLAGS_SEQUENTIAL;
		return 1;
	}
	if (!strcmp(argv[2], "parallel") ||
	    !strcmp(argv[2], "par") ||
	    !strcmp(argv[2], "together")) {
		if (*flag & RUN_SEQUENTIAL)
			goto err;
		*flag |= RUN_PARALLEL;
		g_opt_flags &= ~OPT_FLAGS_SEQUENTIAL;
		g_opt_flags |= OPT_FLAGS_ALL;
		return 1;
	}
err:
	(void)fprintf(stderr, "Cannot have both run sequential "
		"and run parallel in jobfile %s\n",
		jobfile);
	return -1;
}

/*
 *  stress_parse_error()
 *	generic job error message
 */
static void stress_parse_error(
	const uint32_t lineno,
	const char *line)
{
	(void)fprintf(stderr, "error in line %" PRIu32 ": '%s'\n",
		lineno, line);
}

/*
 *  stress_parse_jobfile()
 *	parse a jobfile, turn job commands into
 *	individual stress-ng options
 */
int stress_parse_jobfile(
	const int argc,
	char **argv,
	const char *jobfile)
{
	NOCLOBBER FILE *fp;
	char buf[4096];
	char *new_argv[MAX_ARGS];
	char txt[sizeof(buf)];
	int ret;
	uint32_t flag;
	static uint32_t lineno;

	(void)shim_memset(txt, 0, sizeof(txt));

	if (!jobfile) {
		if (optind >= argc)
			return 0;
		fp = fopen(argv[optind], "r");
		if (!fp)
			return 0;
		optind++;
	} else {
		fp = fopen(jobfile, "r");
	}
	if (!fp) {
		(void)fprintf(stderr, "Cannot open jobfile '%s'\n", jobfile);
		return -1;
	}

	if (setjmp(g_error_env) == 1) {
		stress_parse_error(lineno, txt);
		ret = -1;
		goto err;
	}

	flag = 0;
	ret = -1;

	while (fgets(buf, sizeof(buf), fp)) {
		char *ptr;
		int new_argc = 1;

		(void)shim_memset(new_argv, 0, sizeof(new_argv));
		new_argv[0] = argv[0];
		lineno++;

		/* remove \n */
		stress_str_chop(buf, '\n');
		(void)shim_strscpy(txt, buf, sizeof(txt) - 1);

		/* remove comments */
		stress_str_chop(buf, '#');
		if (!*buf)
			continue;

		/* skip leading blanks */
		for (ptr = buf; ISBLANK(*ptr); ptr++)
			;

		while (new_argc < MAX_ARGS && *ptr) {
			new_argv[new_argc++] = ptr;

			/* eat up chars until eos or blank */
			while (*ptr && !ISBLANK(*ptr))
				ptr++;

			if (!*ptr)
				break;
			*ptr++ = '\0';

			/* skip over blanks */
			while (ISBLANK(*ptr))
				ptr++;
		}

		/* managed to get any tokens? */
		if (new_argc > 1) {
			const size_t len = strlen(new_argv[1]) + 3;
			int rc;
			char *tmp;

			/* Must check for --job -h option! */
			if (!strcmp(new_argv[1], "job") ||
			    !strcmp(new_argv[1], "j")) {
				(void)fprintf(stderr, "Cannot read job file in from a job script!\n");
				goto err;
			}

			/* Check for job run option */
			rc = stress_parse_run(jobfile, new_argc, new_argv, &flag);
			if (rc < 0) {
				ret = -1;
				stress_parse_error(lineno, txt);
				goto err;
			} else if (rc == 1) {
				continue;
			}

			tmp = (char *)malloc(len);
			if (!tmp) {
				(void)fprintf(stderr, "Out of memory parsing '%s'\n", jobfile);
				return -1;
			}

			/* prepend -- to command to make them into stress-ng options */
			(void)snprintf(tmp, len, "--%s", new_argv[1]);
			new_argv[1] = tmp;
			if (stress_parse_opts(new_argc, new_argv, true) != EXIT_SUCCESS) {
				stress_parse_error(lineno, txt);
				ret = -1;
				free(tmp);
				goto err;
			}
			free(tmp);
			new_argv[1] = NULL;
		}
	}
	ret = 0;
err:
	(void)fclose(fp);

	return ret;
}
