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

#define MAX_FSTAT_THREADS	(4)
#define FSTAT_LOOPS		(16)

static volatile bool keep_running;
static sigset_t set;

static const help_t help[] = {
	{ NULL,	"fstat N",	  "start N workers exercising fstat on files" },
	{ NULL,	"fstat-ops N",	  "stop after N fstat bogo operations" },
	{ NULL,	"fstat-dir path", "fstat files in the specified directory" },
	{ NULL,	NULL,		  NULL }
};

/* paths we should never stat */
static const char *blacklist[] = {
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
} stat_info_t;

/* Thread context information */
typedef struct ctxt {
	const args_t 	*args;		/* Stressor args */
	stat_info_t 	*si;		/* path stat information */
	const uid_t	euid;		/* euid of process */
} ctxt_t;

static int stress_set_fstat_dir(const char *opt)
{
	return set_setting("fstat-dir", TYPE_ID_STR, opt);
}

/*
 *  handle_fstat_sigalrm()
 *      catch SIGALRM
 */
static void MLOCKED_TEXT handle_fstat_sigalrm(int signum)
{
	(void)signum;

	keep_running = false;
	g_keep_stressing_flag = false;
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

static void stress_fstat_helper(const ctxt_t *ctxt)
{
	struct stat buf;
#if defined(AT_EMPTY_PATH) && defined(AT_SYMLINK_NOFOLLOW)
	struct shim_statx bufx;
#endif
	stat_info_t *si = ctxt->si;

	if ((stat(si->path, &buf) < 0) && (errno != ENOMEM)) {
		si->ignore |= IGNORE_STAT;
	}
	if ((lstat(si->path, &buf) < 0) && (errno != ENOMEM)) {
		si->ignore |= IGNORE_LSTAT;
	}
#if defined(AT_EMPTY_PATH) && defined(AT_SYMLINK_NOFOLLOW)
	/* Heavy weight statx */
	if ((shim_statx(AT_EMPTY_PATH, si->path, AT_SYMLINK_NOFOLLOW,
		SHIM_STATX_ALL, &bufx) < 0) && (errno != ENOMEM)) {
		si->ignore |= IGNORE_STATX;
	}
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
			return;
		}
		if ((fstat(fd, &buf) < 0) && (errno != ENOMEM))
			si->ignore |= IGNORE_FSTAT;
		(void)close(fd);
	}
}

#if defined(HAVE_LIB_PTHREAD)
/*
 *  stress_fstat_thread
 *	keep exercising a file until
 *	controlling thread triggers an exit
 */
static void *stress_fstat_thread(void *ctxt_ptr)
{
	static void *nowt = NULL;
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	const ctxt_t *ctxt = (const ctxt_t *)ctxt_ptr;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
#if !defined(__APPLE__)
	(void)sigprocmask(SIG_BLOCK, &set, NULL);
#endif

	/*
	 *  According to POSIX.1 a thread should have
	 *  a distinct alternative signal stack.
	 *  However, we block signals in this thread
	 *  so this is probably just totally unncessary.
	 */
	(void)memset(stack, 0, sizeof(stack));
	if (stress_sigaltstack(stack, SIGSTKSZ) < 0)
		return &nowt;

	while (keep_running && g_keep_stressing_flag) {
		size_t i;

		for (i = 0; i < FSTAT_LOOPS; i++)  {
			if (!g_keep_stressing_flag)
				break;
			stress_fstat_helper(ctxt);
		}
		(void)shim_sched_yield();
	}

	return &nowt;
}
#endif

/*
 *  stress_fstat_threads()
 *	create a bunch of threads to thrash a file
 */
static void stress_fstat_threads(const args_t *args, stat_info_t *si, const uid_t euid)
{
	size_t i;
#if defined(HAVE_LIB_PTHREAD)
	pthread_t pthreads[MAX_FSTAT_THREADS];
	int ret[MAX_FSTAT_THREADS];
#endif
	ctxt_t ctxt = {
		.args 	= args,
		.si 	= si,
		.euid	= euid
	};

	keep_running = true;
#if defined(HAVE_LIB_PTHREAD)
	(void)memset(ret, 0, sizeof(ret));
	(void)memset(pthreads, 0, sizeof(pthreads));

	for (i = 0; i < MAX_FSTAT_THREADS; i++) {
		ret[i] = pthread_create(&pthreads[i], NULL,
				stress_fstat_thread, &ctxt);
	}
#endif
	for (i = 0; i < FSTAT_LOOPS; i++) {
		if (!g_keep_stressing_flag)
			break;
		stress_fstat_helper(&ctxt);
	}
	keep_running = false;

#if defined(HAVE_LIB_PTHREAD)
	for (i = 0; i < MAX_FSTAT_THREADS; i++) {
		if (ret[i] == 0)
			(void)pthread_join(pthreads[i], NULL);
	}
#endif
}

/*
 *  stress_fstat()
 *	stress system with fstat
 */
static int stress_fstat(const args_t *args)
{
	stat_info_t *si;
	static stat_info_t *stat_info;
	struct dirent *d;
	NOCLOBBER int ret = EXIT_FAILURE;
	bool stat_some;
	const uid_t euid = geteuid();
	DIR *dp;
	char *fstat_dir = "/dev";

	(void)get_setting("fstat-dir", &fstat_dir);

	if (stress_sighandler(args->name, SIGALRM, handle_fstat_sigalrm, NULL) < 0)
		return EXIT_FAILURE;

	if ((dp = opendir(fstat_dir)) == NULL) {
		pr_err("%s: opendir on %s failed: errno=%d: (%s)\n",
			args->name, fstat_dir, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	/* Cache all the directory entries */
	while ((d = readdir(dp)) != NULL) {
		char path[PATH_MAX];

		if (!g_keep_stressing_flag) {
			ret = EXIT_SUCCESS;
			(void)closedir(dp);
			goto free_cache;
		}

		(void)snprintf(path, sizeof(path), "%s/%s", fstat_dir, d->d_name);
		if (do_not_stat(path))
			continue;
		if ((si = calloc(1, sizeof(*si))) == NULL) {
			pr_err("%s: out of memory\n", args->name);
			(void)closedir(dp);
			goto free_cache;
		}
		if ((si->path = strdup(path)) == NULL) {
			pr_err("%s: out of memory\n", args->name);
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
	do {
		stat_some = false;

		for (si = stat_info; g_keep_stressing_flag && si; si = si->next) {
			if (!keep_stressing())
				break;
			if (si->ignore == IGNORE_ALL)
				continue;
			stress_fstat_threads(args, si, euid);

			stat_some = true;
			inc_counter(args);
		}
	} while (stat_some && keep_stressing());

	ret = EXIT_SUCCESS;
free_cache:
	/* Free cache */
	for (si = stat_info; si; ) {
		stat_info_t *next = si->next;

		free(si->path);
		free(si);
		si = next;
	}

	return ret;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_fstat_dir,	stress_set_fstat_dir },
	{ 0,			NULL }
};

stressor_info_t stress_fstat_info = {
	.stressor = stress_fstat,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
