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
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-pthread.h"

#define MAX_FSTAT_THREADS	(4)
#define FSTAT_LOOPS		(16)

static sigset_t set;

static const stress_help_t help[] = {
	{ NULL,	"fstat N",	  "start N workers exercising fstat on files" },
	{ NULL,	"fstat-dir path", "fstat files in the specified directory" },
	{ NULL,	"fstat-ops N",	  "stop after N fstat bogo operations" },
	{ NULL,	NULL,		  NULL }
};

/* paths we should never stat */
static const char * const blocklist[] = {
	"/dev/watchdog"
};

#define IGNORE_STAT	0x0001
#define IGNORE_LSTAT	0x0002
#define IGNORE_STATX	0x0004
#define IGNORE_FSTAT	0x0008
#define IGNORE_ALL	0x000f

/* stat path information */
typedef struct stat_info {
	struct stat_info *next;		/* next stat_info in list */
	char		*path;		/* path to stat */
	uint16_t	ignore;		/* true to ignore this path */
	bool		access;		/* false if we can't access path */
} stress_stat_info_t;

#if defined(HAVE_LIB_PTHREAD)
typedef struct stress_fstat_pthread_info {
	pthread_t pthread;	/* pthread info */
	int create_ret;		/* return from pthread_create */
	int pthread_ret;	/* return from pthread */
	struct ctxt *ctxt;	/* pointer to generic thread context */
} stress_fstat_pthread_info_t;
#endif

/* Generic thread context information */
typedef struct ctxt {
	stress_args_t *args;	/* stressor args */
	stress_stat_info_t *si;		/* path stat information */
	uid_t euid;			/* euid of process */
	int bad_fd;			/* bad/invalid fd */
} stress_fstat_context_t;

/*
 *  do_not_stat()
 *	Check if file should not be stat'd
 */
static bool PURE do_not_stat(const char *filename)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(blocklist); i++) {
		if (!strncmp(filename, blocklist[i], strlen(blocklist[i])))
			return true;
	}
	return false;
}

/*
 *  stress_fstat_check_buf()
 *	check if some of the stat buf fields have been filled in
 */
static int stress_fstat_check_buf(const struct stat *buf, const struct stat *buf_orig)
{
	if ((buf->st_dev == buf_orig->st_dev) &&
	    (buf->st_ino == buf_orig->st_ino) &&
	    (buf->st_mode == buf_orig->st_mode) &&
	    (buf->st_uid == buf_orig->st_uid) &&
	    (buf->st_gid == buf_orig->st_gid) &&
	    (buf->st_rdev == buf_orig->st_rdev) &&
	    (buf->st_size == buf_orig->st_size)) {
		return -1;
	}
	return 0;
}

static int stress_fstat_helper(const stress_fstat_context_t *ctxt)
{
	struct stat buf, buf_orig;
#if defined(AT_EMPTY_PATH) &&	\
    defined(AT_SYMLINK_NOFOLLOW)
	shim_statx_t bufx;
#endif
	stress_stat_info_t *si = ctxt->si;
	stress_args_t *args = ctxt->args;
	int ret, rc = EXIT_SUCCESS;

	(void)shim_memset(&buf_orig, 0xff, sizeof(buf_orig));
	(void)shim_memset(&buf, 0xff, sizeof(buf));
	ret = stat(si->path, &buf);
	if (ret == 0) {
		if (stress_fstat_check_buf(&buf, &buf_orig) < 0) {
			pr_fail("%s: stat failed to fill in statbuf structure\n", args->name);
			rc = -1;
		}
	} else if ((ret < 0) && (errno != ENOMEM)) {
		si->ignore |= IGNORE_STAT;
	}

	(void)shim_memset(&buf, 0xff, sizeof(buf));
	ret = shim_lstat(si->path, &buf);
	if (ret == 0) {
		if (stress_fstat_check_buf(&buf, &buf_orig) < 0) {
			pr_fail("%s: lstat failed to fill in statbuf structure\n", args->name);
			rc = -1;
		}
	} else if ((ret < 0) && (errno != ENOMEM)) {
		si->ignore |= IGNORE_LSTAT;
	}

#if defined(AT_EMPTY_PATH) &&	\
    defined(AT_SYMLINK_NOFOLLOW)
	/* Heavy weight statx */
	if ((shim_statx(AT_EMPTY_PATH, si->path, AT_SYMLINK_NOFOLLOW,
		SHIM_STATX_ALL, &bufx) < 0) && (errno != ENOMEM)) {
		si->ignore |= IGNORE_STATX;
	}

	/* invalid dfd in statx */
	VOID_RET(int, shim_statx(-1, "baddfd", AT_SYMLINK_NOFOLLOW, SHIM_STATX_ALL, &bufx));

	/* invalid path in statx */
	VOID_RET(int, shim_statx(AT_EMPTY_PATH, "", AT_SYMLINK_NOFOLLOW, SHIM_STATX_ALL, &bufx));

	/* invalid mask in statx */
	VOID_RET(int, shim_statx(AT_EMPTY_PATH, si->path, ~0, SHIM_STATX_ALL, &bufx));

	/* invalid flags in statx */
	VOID_RET(int, shim_statx(AT_EMPTY_PATH, si->path, AT_SYMLINK_NOFOLLOW, ~0U, &bufx));
#else
	UNEXPECTED
#endif
	/*
	 *  Opening /dev files such as /dev/urandom
	 *  may block when running as root, so
	 *  avoid this.
	 */
	if ((si->access) && (ctxt->euid)) {
		int fd;

		fd = open(si->path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) {
			si->access = false;
		} else {
			if ((shim_fstat(fd, &buf) < 0) && (errno != ENOMEM))
				si->ignore |= IGNORE_FSTAT;
			(void)close(fd);
		}
	}

	/* Exercise stat on an invalid path, ENOENT */
	VOID_RET(int, stat("", &buf));

	/* Exercise lstat on an invalid path, ENOENT */
	VOID_RET(int, shim_lstat("", &buf));

	/* Exercise fstat on an invalid fd, EBADF */
	VOID_RET(int, shim_fstat(ctxt->bad_fd, &buf));

	return rc;
}

#if defined(HAVE_LIB_PTHREAD)
/*
 *  stress_fstat_thread
 *	keep exercising a file until
 *	controlling thread triggers an exit
 */
static void *stress_fstat_thread(void *ptr)
{
	stress_fstat_pthread_info_t *pthread_info = (stress_fstat_pthread_info_t *)ptr;
	const stress_fstat_context_t *ctxt = pthread_info->ctxt;

	pthread_info->pthread_ret = 0;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
#if !defined(__APPLE__)
	(void)sigprocmask(SIG_BLOCK, &set, NULL);
#endif
	stress_random_small_sleep();

	while (LIKELY(stress_continue_flag())) {
		size_t i;

		for (i = 0; i < FSTAT_LOOPS; i++) {
			if (stress_fstat_helper(ctxt) < 0) {
				pthread_info->pthread_ret = -1;
				break;
			}
			if (UNLIKELY(!stress_continue_flag()))
				break;
		}
		(void)shim_sched_yield();
	}

	return &g_nowt;
}
#endif

/*
 *  stress_fstat_threads()
 *	create a bunch of threads to thrash a file
 */
static int stress_fstat_threads(stress_args_t *args, stress_stat_info_t *si, const uid_t euid)
{
	size_t i;
	int rc = 0;

	stress_fstat_context_t ctxt = {
		.args = args,
		.si = si,
		.euid = euid,
		.bad_fd = stress_get_bad_fd(),
	};
#if defined(HAVE_LIB_PTHREAD)
	stress_fstat_pthread_info_t pthreads[MAX_FSTAT_THREADS];
#endif

#if defined(HAVE_LIB_PTHREAD)
	(void)shim_memset(pthreads, 0, sizeof(pthreads));

	for (i = 0; i < MAX_FSTAT_THREADS; i++) {
		pthreads[i].ctxt = &ctxt;
		pthreads[i].create_ret =
			pthread_create(&pthreads[i].pthread,
					NULL, stress_fstat_thread, &pthreads[i]);
	}
#endif
	for (i = 0; i < FSTAT_LOOPS; i++) {
		if (stress_fstat_helper(&ctxt) < 0) {
			rc = -1;
			break;
		}
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}

#if defined(HAVE_LIB_PTHREAD)
	for (i = 0; i < MAX_FSTAT_THREADS; i++) {
		if (pthreads[i].create_ret == 0) {
			(void)pthread_join(pthreads[i].pthread, NULL);
			if (pthreads[i].pthread_ret < 0)
				rc = EXIT_FAILURE;
		}
	}
#endif
	return rc;
}

/*
 *  stress_fstat()
 *	stress system with fstat
 */
static int stress_fstat(stress_args_t *args)
{
	stress_stat_info_t *si;
	static stress_stat_info_t *stat_info;
	const struct dirent *d;
	NOCLOBBER int ret = EXIT_FAILURE;
	bool stat_some;
	const uid_t euid = geteuid();
	DIR *dp;
	char *fstat_dir = "/dev";

	(void)stress_get_setting("fstat-dir", &fstat_dir);

	if ((dp = opendir(fstat_dir)) == NULL) {
		pr_err("%s: opendir on %s failed, errno=%d: (%s)\n",
			args->name, fstat_dir, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	/* Cache all the directory entries */
	while ((d = readdir(dp)) != NULL) {
		char path[PATH_MAX];

		if (UNLIKELY(!stress_continue_flag())) {
			ret = EXIT_SUCCESS;
			(void)closedir(dp);
			goto free_cache;
		}

		(void)stress_mk_filename(path, sizeof(path), fstat_dir, d->d_name);
		if (do_not_stat(path))
			continue;
		if ((si = (stress_stat_info_t *)calloc(1, sizeof(*si))) == NULL) {
			pr_err("%s: out of memory allocating %zu bytes%s\n",
				args->name, sizeof(*si),
				stress_get_memfree_str());
			(void)closedir(dp);
			goto free_cache;
		}
		if ((si->path = shim_strdup(path)) == NULL) {
			pr_err("%s: out of memory allocating %zu bytes%s\n",
				args->name, strlen(path),
				stress_get_memfree_str());
			free(si);
			(void)closedir(dp);
			goto free_cache;
		}
		si->ignore = 0;
		si->access = true;
		si->next = stat_info;
		stat_info = si;
	}
	(void)closedir(dp);

	(void)sigfillset(&set);
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		stat_some = false;

		for (si = stat_info; LIKELY(si && stress_continue_flag()); si = si->next) {
			if (UNLIKELY(!stress_continue(args)))
				break;
			if (si->ignore == IGNORE_ALL)
				continue;
			if (stress_fstat_threads(args, si, euid) < 0)
				break;

			stat_some = true;
			stress_bogo_inc(args);
		}
	} while (stat_some && stress_continue(args));

	ret = EXIT_SUCCESS;
free_cache:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* Free cache */
	for (si = stat_info; si; ) {
		stress_stat_info_t *next = si->next;

		free(si->path);
		free(si);
		si = next;
	}

	return ret;
}

static const stress_opt_t opts[] = {
	{ OPT_fstat_dir, "fstat-dir", TYPE_ID_STR, 0, 0, NULL },
	END_OPT,
};

const stressor_info_t stress_fstat_info = {
	.stressor = stress_fstat,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
