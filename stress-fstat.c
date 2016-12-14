/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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

static const char *opt_fstat_dir = "/dev";
static sigjmp_buf jmpbuf;

void stress_set_fstat_dir(const char *optarg)
{
	opt_fstat_dir = optarg;
}

static const char *blacklist[] = {
	"/dev/watchdog"
};

/*
 *  handle_fstat_sigalrm()
 *      catch SIGALRM
 */
static void MLOCKED handle_fstat_sigalrm(int dummy)
{
	(void)dummy;
	opt_do_run = false;

	siglongjmp(jmpbuf, 1);
}

/*
 *  do_not_stat()
 *	Check if file should not be stat'd
 */
static bool do_not_stat(const char *filename)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(blacklist); i++) {
		if (!strncmp(filename, blacklist[i], strlen(blacklist[i])))
			return true;
	}
	return false;
}

/*
 *  stress_fstat()
 *	stress system with fstat
 */
int stress_fstat(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	typedef struct dir_info {
		char	*path;
		bool	ignore;
		bool	noaccess;
		struct dir_info *next;
	} dir_info_t;

	DIR *dp;
	dir_info_t *di;
	static dir_info_t *dir_info;
	struct dirent *d;
	NOCLOBBER int ret = EXIT_FAILURE;
	bool stat_some;
	const uid_t euid = geteuid();

	(void)instance;

	if (stress_sighandler(name, SIGALRM, handle_fstat_sigalrm, NULL) < 0)
		return EXIT_FAILURE;
	if (sigsetjmp(jmpbuf, 0) != 0) {
		ret = EXIT_SUCCESS;
		goto free_cache;
	}

	if ((dp = opendir(opt_fstat_dir)) == NULL) {
		pr_err(stderr, "%s: opendir on %s failed: errno=%d: (%s)\n",
			name, opt_fstat_dir, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	/* Cache all the directory entries */
	while ((d = readdir(dp)) != NULL) {
		char path[PATH_MAX];

		if (!opt_do_run) {
			ret = EXIT_SUCCESS;
			(void)closedir(dp);
			goto free_cache;
		}

		snprintf(path, sizeof(path), "%s/%s", opt_fstat_dir, d->d_name);
		if (do_not_stat(path))
			continue;
		if ((di = calloc(1, sizeof(*di))) == NULL) {
			pr_err(stderr, "%s: out of memory\n", name);
			(void)closedir(dp);
			goto free_cache;
		}
		if ((di->path = strdup(path)) == NULL) {
			pr_err(stderr, "%s: out of memory\n", name);
			free(di);
			(void)closedir(dp);
			goto free_cache;
		}
		di->ignore = false;
		di->noaccess = false;
		di->next = dir_info;
		dir_info = di;
	}
	(void)closedir(dp);

	do {
		stat_some = false;

		for (di = dir_info; opt_do_run && di; di = di->next) {
			int fd;
			struct stat buf;

			if (!opt_do_run || (max_ops && *counter >= max_ops))
				goto aborted;

			if (di->ignore)
				continue;

			if ((stat(di->path, &buf) < 0) &&
			    (errno != ENOMEM)) {
				di->ignore = true;
				continue;
			}
			if ((lstat(di->path, &buf) < 0) &&
			    (errno != ENOMEM)) {
				di->ignore = true;
				continue;
			}
			if (di->noaccess)
				continue;

			/*
			 *  Opening /dev files such as /dev/urandom
			 *  may block when running as root, so
			 *  avoid this.
			 */
			if (!euid) {
				fd = open(di->path, O_RDONLY | O_NONBLOCK);
				if (fd < 0) {
					di->noaccess = true;
					continue;
				}

				if ((fstat(fd, &buf) < 0) &&
				    (errno != ENOMEM)) {
					di->ignore = true;
					(void)close(fd);
					continue;
				}

				(void)close(fd);
			}
			stat_some = true;
			(*counter)++;
		}
	} while (stat_some && opt_do_run && (!max_ops || *counter < max_ops));

aborted:
	ret = EXIT_SUCCESS;
free_cache:
	/* Free cache */
	for (di = dir_info; di; ) {
		dir_info_t *next = di->next;

		free(di->path);
		free(di);
		di = next;
	}

	return ret;
}
