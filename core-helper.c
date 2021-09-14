/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
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
#include "git-commit-id.h"

#if !defined(PR_SET_DISABLE)
#define SUID_DUMP_DISABLE	(0)       /* No setuid dumping */
#endif
#if !defined(SUID_DUMP_USER)
#define SUID_DUMP_USER		(1)       /* Dump as user of process */
#endif

#if defined(NSIG)
#define STRESS_NSIG	NSIG
#elif defined(_NSIG)
#define STRESS_NSIG	_NSIG
#endif

#if defined(__TINYC__) || defined(__PCC__)
int __dso_handle;
#endif

static bool stress_stack_check_flag;

typedef struct {
	const int  signum;
	const char *name;
} stress_sig_name_t;

#define SIG_NAME(x) { x, #x }

static const stress_sig_name_t sig_names[] = {
#if defined(SIGABRT)
	SIG_NAME(SIGABRT),
#endif
#if defined(SIGALRM)
	SIG_NAME(SIGALRM),
#endif
#if defined(SIGBUS)
	SIG_NAME(SIGBUS),
#endif
#if defined(SIGCHLD)
	SIG_NAME(SIGCHLD),
#endif
#if defined(SIGCLD)
	SIG_NAME(SIGCLD),
#endif
#if defined(SIGCONT)
	SIG_NAME(SIGCONT),
#endif
#if defined(SIGEMT)
	SIG_NAME(SIGEMT),
#endif
#if defined(SIGFPE)
	SIG_NAME(SIGFPE),
#endif
#if defined(SIGHUP)
	SIG_NAME(SIGHUP),
#endif
#if defined(SIGILL)
	SIG_NAME(SIGILL),
#endif
#if defined(SIGINFO)
	SIG_NAME(SIGINFO),
#endif
#if defined(SIGINT)
	SIG_NAME(SIGINT),
#endif
#if defined(SIGIO)
	SIG_NAME(SIGIO),
#endif
#if defined(SIGIOT)
	SIG_NAME(SIGIOT),
#endif
#if defined(SIGKILL)
	SIG_NAME(SIGKILL),
#endif
#if defined(SIGLOST)
	SIG_NAME(SIGLOST),
#endif
#if defined(SIGPIPE)
	SIG_NAME(SIGPIPE),
#endif
#if defined(SIGPOLL)
	SIG_NAME(SIGPOLL),
#endif
#if defined(SIGPROF)
	SIG_NAME(SIGPROF),
#endif
#if defined(SIGPWR)
	SIG_NAME(SIGPWR),
#endif
#if defined(SIGQUIT)
	SIG_NAME(SIGQUIT),
#endif
#if defined(SIGSEGV)
	SIG_NAME(SIGSEGV),
#endif
#if defined(SIGSTKFLT)
	SIG_NAME(SIGSTKFLT),
#endif
#if defined(SIGSTOP)
	SIG_NAME(SIGSTOP),
#endif
#if defined(SIGTSTP)
	SIG_NAME(SIGTSTP),
#endif
#if defined(SIGSYS)
	SIG_NAME(SIGSYS),
#endif
#if defined(SIGTERM)
	SIG_NAME(SIGTERM),
#endif
#if defined(SIGTRAP)
	SIG_NAME(SIGTRAP),
#endif
#if defined(SIGTTIN)
	SIG_NAME(SIGTTIN),
#endif
#if defined(SIGTTOU)
	SIG_NAME(SIGTTOU),
#endif
#if defined(SIGUNUSED)
	SIG_NAME(SIGUNUSED),
#endif
#if defined(SIGURG)
	SIG_NAME(SIGURG),
#endif
#if defined(SIGUSR1)
	SIG_NAME(SIGUSR1),
#endif
#if defined(SIGUSR2)
	SIG_NAME(SIGUSR2),
#endif
#if defined(SIGVTALRM)
	SIG_NAME(SIGVTALRM),
#endif
#if defined(SIGXCPU)
	SIG_NAME(SIGXCPU),
#endif
#if defined(SIGXFSZ)
	SIG_NAME(SIGXFSZ),
#endif
#if defined(SIGWINCH)
	SIG_NAME(SIGWINCH),
#endif
};

static char *stress_temp_path;

/*
 *  stress_temp_path_free()
 *	free and NULLify temporary file path
 */
void stress_temp_path_free(void)
{
	if (stress_temp_path)
		free(stress_temp_path);

	stress_temp_path = NULL;
}

/*
 *  stress_set_temp_path()
 *	set temporary file path, default
 *	is . - current dir
 */
int stress_set_temp_path(const char *path)
{
	stress_temp_path_free();

	stress_temp_path = stress_const_optdup(path);
	if (!stress_temp_path)
		return -1;

	if (access(path, R_OK | W_OK) < 0) {
		(void)fprintf(stderr, "aborting: temp-path '%s' must be readable "
			"and writeable\n", path);
		return -1;
	}

	return 0;
}

/*
 *  stress_get_temp_path()
 *	get temporary file path, return "." if null
 */
const char *stress_get_temp_path(void)
{
	if (!stress_temp_path)
		return ".";
	return stress_temp_path;
}

/*
 *  stress_mk_filename()
 *	generate a full file name from a path and filename
 */
size_t stress_mk_filename(
	char *fullname,
	const size_t fullname_len,
	const char *pathname,
	const char *filename)
{
	/*
	 *  This may not be efficient, but it works. Do not
	 *  be tempted to optimize this, it is not used frequently
	 *  and is not a CPU bottleneck.
	 */
	(void)shim_strlcpy(fullname, pathname, fullname_len);
	(void)shim_strlcat(fullname, "/", fullname_len);
	return shim_strlcat(fullname, filename, fullname_len);
}

/*
 *  stress_get_pagesize()
 *	get pagesize
 */
size_t stress_get_pagesize(void)
{
	static size_t page_size = 0;

	/* Use cached size */
	if (page_size > 0)
		return page_size;

#if defined(_SC_PAGESIZE)
	{
		/* Use modern sysconf */
		long sz = sysconf(_SC_PAGESIZE);
		if (sz > 0) {
			page_size = (size_t)sz;
			return page_size;
		}
	}
#endif
#if defined(HAVE_GETPAGESIZE)
	{
		/* Use deprecated getpagesize */
		long sz = getpagesize();
		if (sz > 0) {
			page_size = (size_t)sz;
			return page_size;
		}
	}
#endif
	/* Guess */
	page_size = PAGE_4K;
	return page_size;
}

/*
 *  stress_get_processors_online()
 *	get number of processors that are online
 */
int32_t stress_get_processors_online(void)
{
	static int32_t processors_online = 0;

	if (processors_online > 0)
		return processors_online;

#if defined(_SC_NPROCESSORS_ONLN)
	processors_online = (int32_t)sysconf(_SC_NPROCESSORS_ONLN);
	if (processors_online < 0)
		processors_online = 1;
#else
	processors_online = 1;
#endif
	return processors_online;
}

/*
 *  stress_get_processors_configured()
 *	get number of processors that are configured
 */
int32_t stress_get_processors_configured(void)
{
	static int32_t processors_configured = 0;

	if (processors_configured > 0)
		return processors_configured;

#if defined(_SC_NPROCESSORS_CONF)
	processors_configured = (int32_t)sysconf(_SC_NPROCESSORS_CONF);
	if (processors_configured < 0)
		processors_configured = stress_get_processors_online();
#else
	processors_configured = 1;
#endif
	return processors_configured;
}

/*
 *  stress_get_ticks_per_second()
 *	get number of ticks perf second
 */
int32_t stress_get_ticks_per_second(void)
{
#if defined(_SC_CLK_TCK)
	static int32_t ticks_per_second = 0;

	if (ticks_per_second > 0)
		return ticks_per_second;

	ticks_per_second = (int32_t)sysconf(_SC_CLK_TCK);
	return ticks_per_second;
#else
	return -1;
#endif
}

/*
 *  stress_get_memlimits()
 *	get SHMALL and memory in system
 *	these are set to zero on failure
 */
void stress_get_memlimits(
	size_t *shmall,
	size_t *freemem,
	size_t *totalmem,
	size_t *freeswap)
{
#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO)
	struct sysinfo info;
	FILE *fp;
#endif
	*shmall = 0;
	*freemem = 0;
	*totalmem = 0;
	*freeswap = 0;

#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO)
	(void)memset(&info, 0, sizeof(info));

	if (sysinfo(&info) == 0) {
		*freemem = info.freeram * info.mem_unit;
		*totalmem = info.totalram * info.mem_unit;
		*freeswap = info.freeswap * info.mem_unit;
	}

	fp = fopen("/proc/sys/kernel/shmall", "r");
	if (!fp)
		return;

	if (fscanf(fp, "%zu", shmall) != 1) {
		(void)fclose(fp);
		return;
	}
	(void)fclose(fp);
#endif
}

#if defined(_SC_AVPHYS_PAGES)
#define STRESS_SC_PAGES	_SC_AVPHYS_PAGES
#elif defined(_SC_PHYS_PAGES)
#define STRESS_SC_PAGES	_SC_PHYS_PAGES
#endif

/*
 *  stress_get_phys_mem_size()
 *	get size of physical memory still available, 0 if failed
 */
uint64_t stress_get_phys_mem_size(void)
{
#if defined(STRESS_SC_PAGES)
	uint64_t phys_pages = 0;
	const size_t page_size = stress_get_pagesize();
	const uint64_t max_pages = ~0ULL / page_size;

	phys_pages = (uint64_t)sysconf(STRESS_SC_PAGES);
	/* Avoid overflow */
	if (phys_pages > max_pages)
		phys_pages = max_pages;
	return phys_pages * page_size;
#else
	return 0ULL;
#endif
}

/*
 *  stress_get_filesystem_size()
 *	get size of free space still available on the
 *	file system where stress temporary path is located,
 *	return 0 if failed
 */
uint64_t stress_get_filesystem_size(void)
{
#if defined(HAVE_SYS_STATVFS_H)
	int rc;
	struct statvfs buf;
	fsblkcnt_t blocks, max_blocks;
	const char *path = stress_get_temp_path();

	if (!path)
		return 0;

	(void)memset(&buf, 0, sizeof(buf));
	rc = statvfs(path, &buf);
	if (rc < 0)
		return 0;

	max_blocks = (~(fsblkcnt_t)0) / buf.f_bsize;
	blocks = buf.f_bavail;

	if (blocks > max_blocks)
		blocks = max_blocks;

	return (uint64_t)buf.f_bsize * blocks;
#else
	return 0ULL;
#endif
}

/*
 *  stress_get_filesystem_available_inodes()
 *	get number of free available inodes on the current stress
 *	temporary path, return 0 if failed
 */
uint64_t stress_get_filesystem_available_inodes(void)
{
#if defined(HAVE_SYS_STATVFS_H)
	int rc;
	struct statvfs buf;
	const char *path = stress_get_temp_path();

	if (!path)
		return 0;

	(void)memset(&buf, 0, sizeof(buf));
	rc = statvfs(path, &buf);
	if (rc < 0)
		return 0;

	return (uint64_t)buf.f_favail;
#else
	return 0ULL;
#endif
}

/*
 *  stress_set_nonblock()
 *	try to make fd non-blocking
 */
int stress_set_nonblock(const int fd)
{
	int flags;
#if defined(O_NONBLOCK)

	if ((flags = fcntl(fd, F_GETFL, 0)) < 0)
		flags = 0;
	return fcntl(fd, F_SETFL, O_NONBLOCK | flags);
#else
	flags = 1;
	return ioctl(fd, FIOBIO, &flags);
#endif
}

/*
 *  stress_get_load_avg()
 *	get load average
 */
int stress_get_load_avg(
	double *min1,
	double *min5,
	double *min15)
{
#if NEED_GLIBC(2, 2, 0) &&	\
    !defined(__UCLIBC__)
	int rc;
	double loadavg[3];

	loadavg[0] = 0.0;
	loadavg[1] = 0.0;
	loadavg[2] = 0.0;

	rc = getloadavg(loadavg, 3);
	if (rc < 0)
		goto fail;

	*min1 = loadavg[0];
	*min5 = loadavg[1];
	*min15 = loadavg[2];

	return 0;
fail:
#elif defined(HAVE_SYS_SYSINFO_H) &&	\
      defined(HAVE_SYSINFO)
	struct sysinfo info;
	const double scale = 1.0 / (double)(1 << SI_LOAD_SHIFT);

	if (sysinfo(&info) < 0)
		goto fail;

	*min1 = info.loads[0] * scale;
	*min5 = info.loads[1] * scale;
	*min15 = info.loads[2] * scale;

	return 0;
fail:
#endif
	*min1 = *min5 = *min15 = 0.0;
	return -1;
}

/*
 *  stress_parent_died_alarm()
 *	send child SIGALRM if the parent died
 */
void stress_parent_died_alarm(void)
{
#if defined(HAVE_PRCTL) &&		\
    defined(HAVE_SYS_PRCTL_H) &&	\
    defined(PR_SET_PDEATHSIG)
	(void)prctl(PR_SET_PDEATHSIG, SIGALRM);
#endif
}

/*
 *  stress_process_dumpable()
 *	set dumpable flag, e.g. produce a core dump or not,
 *	don't print an error if these fail, it's not that
 *	critical
 */
int stress_process_dumpable(const bool dumpable)
{
	int fd, rc = 0;

#if defined(RLIMIT_CORE)
	{
		struct rlimit lim;
		int ret;

		ret = getrlimit(RLIMIT_CORE, &lim);
		if (ret == 0) {
			lim.rlim_cur = 0;
			(void)setrlimit(RLIMIT_CORE, &lim);
		}
		lim.rlim_cur = 0;
		lim.rlim_max = 0;
		(void)setrlimit(RLIMIT_CORE, &lim);
	}
#endif

	/*
	 *  changing PR_SET_DUMPABLE also affects the
	 *  oom adjust capability, so for now, we disable
	 *  this as I'd rather have a oom'able process when
	 *  memory gets constrained. Don't enable this
	 *  unless one checks that processes able oomable!
	 */
#if 0 && defined(HAVE_PRCTL) &&		\
    defined(HAVE_SYS_PRCTL_H) &&	\
    defined(PR_SET_DUMPABLE)
	(void)prctl(PR_SET_DUMPABLE,
		dumpable ? SUID_DUMP_USER : SUID_DUMP_DISABLE);
#endif
	if ((fd = open("/proc/self/coredump_filter", O_WRONLY)) >= 0) {
		char const *str =
			dumpable ? "0x33" : "0x00";

		if (write(fd, str, strlen(str)) < 0)
			rc = -1;
		(void)close(fd);
	}
	return rc;
}

/*
 *  stress_set_timer_slackigned_longns()
 *	set timer slack in nanoseconds
 */
int stress_set_timer_slack_ns(const char *opt)
{
#if defined(HAVE_PRCTL_TIMER_SLACK)
	uint32_t timer_slack;

	timer_slack = stress_get_uint32(opt);
	(void)stress_set_setting("timer-slack", TYPE_ID_UINT32, &timer_slack);
#else
	(void)opt;
#endif
	return 0;
}

/*
 *  stress_set_timer_slack()
 *	set timer slack
 */
void stress_set_timer_slack(void)
{
#if defined(HAVE_PRCTL) && 		\
    defined(HAVE_SYS_PRCTL_H) &&	\
    defined(HAVE_PRCTL_TIMER_SLACK)
	uint32_t timer_slack;

	if (stress_get_setting("timer-slack", &timer_slack))
		(void)prctl(PR_SET_TIMERSLACK, timer_slack);
#endif
}

/*
 *  stress_set_proc_name_init()
 *	init setproctitle if supported
 */
void stress_set_proc_name_init(int argc, char *argv[], char *envp[])
{
#if defined(HAVE_BSD_UNISTD_H) &&	\
    defined(HAVE_SETPROCTITLE)
	(void)setproctitle_init(argc, argv, envp);
#else
	(void)argc;
	(void)argv;
	(void)envp;
#endif
}

/*
 *  stress_set_proc_name()
 *	Set process name, we don't care if it fails
 */
void stress_set_proc_name(const char *name)
{
	(void)name;

	if (g_opt_flags & OPT_FLAGS_KEEP_NAME)
		return;

#if defined(HAVE_BSD_UNISTD_H) &&	\
    defined(HAVE_SETPROCTITLE)
	/* Sets argv[0] */
	setproctitle("-%s", name);
#endif
#if defined(HAVE_PRCTL) &&		\
    defined(HAVE_SYS_PRCTL_H) &&	\
    defined(PR_SET_NAME)
	/* Sets the comm field */
	(void)prctl(PR_SET_NAME, name);
#endif
}

/*
 *  stress_set_proc_state
 *	set process name based on run state, see
 *	macros STRESS_STATE_*
 */
void stress_set_proc_state(const char *name, const int state)
{
	static const char *stress_states[] = {
		"start",
		"init",
		"run",
		"deinit",
		"stop",
		"exit",
		"wait"
	};

	(void)name;

	if (g_opt_flags & OPT_FLAGS_KEEP_NAME)
		return;

	if ((state < 0) || (state >= (int)SIZEOF_ARRAY(stress_states)))
		return;

#if defined(HAVE_BSD_UNISTD_H) &&	\
    defined(HAVE_SETPROCTITLE)
	setproctitle("-%s [%s]", name, stress_states[state]);
#endif
}

/*
 *  stress_munge_underscore()
 *	turn '_' to '-' in strings
 */
char *stress_munge_underscore(const char *str)
{
	static char munged[128];
	char *dst;
	const char *src;
	const size_t str_len = strlen(str);
	const ssize_t len = (ssize_t)STRESS_MINIMUM(str_len, sizeof(munged) - 1);

	for (src = str, dst = munged; *src && (dst - munged) < len; src++)
		*dst++ = (*src == '_' ? '-' : *src);

	*dst = '\0';

	return munged;
}

/*
 *  stress_get_stack_direction_helper()
 *	helper to determine direction of stack
 */
static ssize_t NOINLINE OPTIMIZE0 stress_get_stack_direction_helper(const uint8_t *val1)
{
	const uint8_t val2 = 0;
	const ssize_t diff = &val2 - (const uint8_t *)val1;

	return (diff > 0) - (diff < 0);
}

/*
 *  stress_get_stack_direction()
 *      determine which way the stack goes, up / down
 *	just pass in any var on the stack before calling
 *	return:
 *		 1 - stack goes down (conventional)
 *		 0 - error
 *	  	-1 - stack goes up (unconventional)
 */
ssize_t stress_get_stack_direction(void)
{
	uint8_t val1 = 0;
	uint8_t waste[64];

	waste[(sizeof waste) - 1] = 0;
	return stress_get_stack_direction_helper(&val1);
}

/*
 *  stress_get_stack_top()
 *	Get the stack top given the start and size of the stack,
 *	offset by a bit of slop. Assumes stack is > 64 bytes
 */
void *stress_get_stack_top(void *start, size_t size)
{
	const size_t offset = stress_get_stack_direction() < 0 ? (size - 64) : 64;

	return (void *)((char *)start + offset);
}

/*
 *  stress_uint64_zero()
 *	return uint64 zero in way that force less smart
 *	static analysers to realise we are doing this
 *	to force a division by zero. I'd like to have
 *	a better solution than this ghastly way.
 */
uint64_t stress_uint64_zero(void)
{
	return g_shared->zero;
}

/*
 *  stress_base36_encode_uint64()
 *	encode 64 bit hash of filename into a unique base 36
 *	filename of up to 13 chars long + 1 char eos
 */
static void stress_base36_encode_uint64(char dst[14], uint64_t val)
{
        static const char b36[] = "abcdefghijklmnopqrstuvwxyz0123456789";
        const int b = 36;
        char *ptr = dst;

        while (val) {
                *ptr++ = b36[val % b];
                val /= b;
        }
        *ptr = '\0';
}

/*
 *  stress_temp_hash_truncate()
 *	filenames may be too long for the underlying filesystem
 *	so workaround this by hashing them into a 64 bit hex
 *	filename.
 */
static void stress_temp_hash_truncate(char *filename)
{
	size_t f_namemax = 16;
	size_t len = strlen(filename);
#if defined(HAVE_SYS_STATVFS_H)
	struct statvfs buf;

	(void)memset(&buf, 0, sizeof(buf));
	if (statvfs(stress_get_temp_path(), &buf) == 0)
		f_namemax = buf.f_namemax;
#endif

	if (strlen(filename) > f_namemax) {
		uint32_t upper, lower;
		uint64_t val;

		upper = stress_hash_jenkin((uint8_t *)filename, len);
		lower = stress_hash_pjw(filename);
		val = ((uint64_t)upper << 32) | lower;

		stress_base36_encode_uint64(filename, val);
	}
}

/*
 *  stress_temp_filename()
 *      construct a temp filename
 */
int stress_temp_filename(
	char *path,
	const size_t len,
	const char *name,
	const pid_t pid,
	const uint32_t instance,
	const uint64_t magic)
{
	char directoryname[PATH_MAX];
	char filename[PATH_MAX];

	(void)snprintf(directoryname, sizeof(directoryname),
		"tmp-%s-%d-%" PRIu32,
		name, (int)pid, instance);
	stress_temp_hash_truncate(directoryname);

	(void)snprintf(filename, sizeof(filename),
		"%s-%d-%" PRIu32 "-%" PRIu64,
		name, (int)pid, instance, magic);
	stress_temp_hash_truncate(filename);

	return snprintf(path, len, "%s/%s/%s",
		stress_get_temp_path(), directoryname, filename);
}

/*
 *  stress_temp_filename_args()
 *      construct a temp filename using info from args
 */
int stress_temp_filename_args(
	const stress_args_t *args,
	char *path,
	const size_t len,
	const uint64_t magic)
{
	return stress_temp_filename(path, len, args->name,
		args->pid, args->instance, magic);
}

/*
 *  stress_temp_dir()
 *	create a temporary directory name
 */
int stress_temp_dir(
	char *path,
	const size_t len,
	const char *name,
	const pid_t pid,
	const uint32_t instance)
{
	char directoryname[256];

	(void)snprintf(directoryname, sizeof(directoryname),
		"tmp-%s-%d-%" PRIu32,
		name, (int)pid, instance);
	stress_temp_hash_truncate(directoryname);

	return snprintf(path, len, "%s/%s",
		stress_get_temp_path(), directoryname);
}

/*
 *  stress_temp_dir_args()
 *	create a temporary directory name using info from args
 */
int stress_temp_dir_args(
	const stress_args_t *args,
	char *path,
	const size_t len)
{
	return stress_temp_dir(path, len,
		args->name, args->pid, args->instance);
}

/*
 *   stress_temp_dir_mk()
 *	create a temporary directory
 */
int stress_temp_dir_mk(
	const char *name,
	const pid_t pid,
	const uint32_t instance)
{
	int ret;
	char tmp[PATH_MAX];

	stress_temp_dir(tmp, sizeof(tmp), name, pid, instance);
	ret = mkdir(tmp, S_IRWXU);
	if (ret < 0) {
		ret = -errno;
		pr_fail("%s: mkdir '%s' failed, errno=%d (%s)\n",
			name, tmp, errno, strerror(errno));
		(void)unlink(tmp);
	}

	return ret;
}

/*
 *   stress_temp_dir_mk_args()
 *	create a temporary director using info from args
 */
int stress_temp_dir_mk_args(const stress_args_t *args)
{
	return stress_temp_dir_mk(args->name, args->pid, args->instance);
}

/*
 *  stress_temp_dir_rm()
 *	remove a temporary directory
 */
int stress_temp_dir_rm(
	const char *name,
	const pid_t pid,
	const uint32_t instance)
{
	int ret;
	char tmp[PATH_MAX + 1];

	stress_temp_dir(tmp, sizeof(tmp), name, pid, instance);
	ret = rmdir(tmp);
	if (ret < 0) {
		ret = -errno;
		pr_fail("%s: rmdir '%s' failed, errno=%d (%s)\n",
			name, tmp, errno, strerror(errno));
	}

	return ret;
}

/*
 *  stress_temp_dir_rm_args()
 *	remove a temporary directory using info from args
 */
int stress_temp_dir_rm_args(const stress_args_t *args)
{
	return stress_temp_dir_rm(args->name, args->pid, args->instance);
}

/*
 *  stress_cwd_readwriteable()
 *	check if cwd is read/writeable
 */
void stress_cwd_readwriteable(void)
{
	char path[PATH_MAX];

	if (getcwd(path, sizeof(path)) == NULL) {
		pr_dbg("cwd: Cannot determine current working directory\n");
		return;
	}
	if (access(path, R_OK | W_OK)) {
		pr_inf("Working directory %s is not read/writeable, "
			"some I/O tests may fail\n", path);
		return;
	}
}

/*
 *  stress_signal_name()
 *	return string version of signal number, NULL if not found
 */
const char *stress_signal_name(const int signum)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(sig_names); i++) {
		if (signum == sig_names[i].signum)
			return sig_names[i].name;
	}
	return NULL;
}

/*
 *  stress_strsignal()
 *	signum to human readable string
 */
const char *stress_strsignal(const int signum)
{
	static char buffer[40];
	const char *str = stress_signal_name(signum);

	if (str)
		(void)snprintf(buffer, sizeof(buffer), "signal %d '%s'",
			signum, str);
	else
		(void)snprintf(buffer, sizeof(buffer), "signal %d", signum);
	return buffer;
}

/*
 *  stress_strnrnd()
 *	fill string with random chars
 */
void stress_strnrnd(char *str, const size_t len)
{
	const char *end = str + len;

	while (str < end - 1)
		*str++ = (stress_mwc8() % 26) + 'a';

	*str = '\0';
}

/*
 *  pr_run_info()
 *	short info about the system we are running stress-ng on
 *	for the -v option
 */
void pr_runinfo(void)
{
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	struct utsname uts;
#endif
#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO)
	struct sysinfo info;
#endif
	if (!(g_opt_flags & PR_DEBUG))
		return;

	if (sizeof(STRESS_GIT_COMMIT_ID) > 1) {
		pr_dbg("%s %s g%12.12s\n",
			g_app_name, VERSION, STRESS_GIT_COMMIT_ID);
	} else {
		pr_dbg("%s %s\n",
			g_app_name, VERSION);
	}

#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	if (uname(&uts) == 0) {
		pr_dbg("system: %s %s %s %s %s\n",
			uts.sysname, uts.nodename, uts.release,
			uts.version, uts.machine);
	}
#endif
#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO)
	/* Keep static analyzer happy */
	(void)memset(&info, 0, sizeof(info));
	if (sysinfo(&info) == 0) {
		char ram_t[32], ram_f[32], ram_s[32];

		stress_uint64_to_str(ram_t, sizeof(ram_t), (uint64_t)info.totalram);
		stress_uint64_to_str(ram_f, sizeof(ram_f), (uint64_t)info.freeram);
		stress_uint64_to_str(ram_s, sizeof(ram_s), (uint64_t)info.freeswap);
		pr_dbg("RAM total: %s, RAM free: %s, swap free: %s\n", ram_t, ram_f, ram_s);
	}
#endif
}

/*
 *  pr_yaml_runinfo()
 *	log info about the system we are running stress-ng on
 */
void pr_yaml_runinfo(FILE *yaml)
{
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	struct utsname uts;
#endif
#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO)
	struct sysinfo info;
#endif
	time_t t;
	struct tm *tm = NULL;
	const size_t hostname_len = stress_hostname_length();
	char hostname[hostname_len];
	const char *user = shim_getlogin();

	pr_yaml(yaml, "system-info:\n");
	if (time(&t) != ((time_t)-1))
		tm = localtime(&t);

	pr_yaml(yaml, "      stress-ng-version: " VERSION "\n");
	pr_yaml(yaml, "      run-by: %s\n", user ? user : "unknown");
	if (tm) {
		pr_yaml(yaml, "      date-yyyy-mm-dd: %4.4d:%2.2d:%2.2d\n",
			tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
		pr_yaml(yaml, "      time-hh-mm-ss: %2.2d:%2.2d:%2.2d\n",
			tm->tm_hour, tm->tm_min, tm->tm_sec);
		pr_yaml(yaml, "      epoch-secs: %ld\n", (long)t);
	}
	if (!gethostname(hostname, sizeof(hostname)))
		pr_yaml(yaml, "      hostname: %s\n", hostname);
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	if (uname(&uts) == 0) {
		pr_yaml(yaml, "      sysname: %s\n", uts.sysname);
		pr_yaml(yaml, "      nodename: %s\n", uts.nodename);
		pr_yaml(yaml, "      release: %s\n", uts.release);
		pr_yaml(yaml, "      version: '%s'\n", uts.version);
		pr_yaml(yaml, "      machine: %s\n", uts.machine);
	}
#endif
#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO)
	(void)memset(&info, 0, sizeof(info));
	if (sysinfo(&info) == 0) {
		pr_yaml(yaml, "      uptime: %ld\n", info.uptime);
		pr_yaml(yaml, "      totalram: %lu\n", info.totalram);
		pr_yaml(yaml, "      freeram: %lu\n", info.freeram);
		pr_yaml(yaml, "      sharedram: %lu\n", info.sharedram);
		pr_yaml(yaml, "      bufferram: %lu\n", info.bufferram);
		pr_yaml(yaml, "      totalswap: %lu\n", info.totalswap);
		pr_yaml(yaml, "      freeswap: %lu\n", info.freeswap);
	}
#endif
	pr_yaml(yaml, "      pagesize: %zd\n", stress_get_pagesize());
	pr_yaml(yaml, "      cpus: %" PRId32 "\n", stress_get_processors_configured());
	pr_yaml(yaml, "      cpus-online: %" PRId32 "\n", stress_get_processors_online());
	pr_yaml(yaml, "      ticks-per-second: %" PRId32 "\n", stress_get_ticks_per_second());
	pr_yaml(yaml, "\n");
}

/*
 *  stress_cache_alloc()
 *	allocate shared cache buffer
 */
int stress_cache_alloc(const char *name)
{
#if defined(__linux__)
	stress_cpus_t *cpu_caches;
	stress_cpu_cache_t *cache = NULL;
	uint16_t max_cache_level = 0;
#endif

#if !defined(__linux__)
	g_shared->mem_cache_size = MEM_CACHE_SIZE;
#else
	cpu_caches = stress_get_all_cpu_cache_details();
	if (!cpu_caches) {
		if (stress_warn_once())
			pr_dbg("%s: using defaults, cannot determine cache details\n", name);
		g_shared->mem_cache_size = MEM_CACHE_SIZE;
		goto init_done;
	}

	max_cache_level = stress_get_max_cache_level(cpu_caches);
	if (max_cache_level == 0) {
		if (stress_warn_once())
			pr_dbg("%s: using defaults, cannot determine cache level details\n", name);
		g_shared->mem_cache_size = MEM_CACHE_SIZE;
		goto init_done;
	}
	if (g_shared->mem_cache_level > max_cache_level) {
		if (stress_warn_once())
			pr_dbg("%s: using cache maximum level L%d\n", name,
				max_cache_level);
		g_shared->mem_cache_level = max_cache_level;
	}

	cache = stress_get_cpu_cache(cpu_caches, g_shared->mem_cache_level);
	if (!cache) {
		if (stress_warn_once())
			pr_dbg("%s: using built-in defaults as no suitable "
				"cache found\n", name);
		g_shared->mem_cache_size = MEM_CACHE_SIZE;
		goto init_done;
	}

	if (g_shared->mem_cache_ways > 0) {
		uint64_t way_size;

		if (g_shared->mem_cache_ways > cache->ways) {
			if (stress_warn_once())
				pr_inf("%s: cache way value too high - "
					"defaulting to %d (the maximum)\n",
					name, cache->ways);
			g_shared->mem_cache_ways = cache->ways;
		}
		way_size = cache->size / cache->ways;

		/* only fill the specified number of cache ways */
		g_shared->mem_cache_size = way_size * g_shared->mem_cache_ways;
	} else {
		/* fill the entire cache */
		g_shared->mem_cache_size = cache->size;
	}

	if (!g_shared->mem_cache_size) {
		if (stress_warn_once())
			pr_dbg("%s: using built-in defaults as "
				"unable to determine cache size\n", name);
		g_shared->mem_cache_size = MEM_CACHE_SIZE;
	}
init_done:
	stress_free_cpu_caches(cpu_caches);
#endif
	g_shared->mem_cache =
		(uint8_t *)mmap(NULL, g_shared->mem_cache_size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (g_shared->mem_cache == MAP_FAILED) {
		g_shared->mem_cache = NULL;
		pr_err("%s: failed to mmap shared cache buffer, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return -1;
	}
	if (stress_warn_once())
		pr_dbg("%s: shared cache buffer size: %" PRIu64 "K\n",
			name, g_shared->mem_cache_size / 1024);

	return 0;
}

/*
 *  stress_cache_free()
 *	free shared cache buffer
 */
void stress_cache_free(void)
{
	if (g_shared->mem_cache)
		(void)munmap((void *)g_shared->mem_cache, g_shared->mem_cache_size);
}

/*
 *  system_write()
 *	write a buffer to a /sys or /proc entry
 */
ssize_t system_write(
	const char *path,
	const char *buf,
	const size_t buf_len)
{
	int fd;
	ssize_t ret;

	fd = open(path, O_WRONLY);
	if (fd < 0)
		return -errno;
	ret = write(fd, buf, buf_len);
	if (ret < (ssize_t)buf_len)
		ret = -errno;
	(void)close(fd);

	return ret;
}

/*
 *  system_read()
 *	read a buffer from a /sys or /proc entry
 */
ssize_t system_read(
	const char *path,
	char *buf,
	const size_t buf_len)
{
	int fd;
	ssize_t ret;

	(void)memset(buf, 0, buf_len);

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;
	ret = read(fd, buf, buf_len);
	if (ret < 0) {
		buf[0] = '\0';
		ret = -errno;
	}
	(void)close(fd);
	if ((ssize_t)buf_len == ret)
		buf[buf_len - 1] = '\0';
	else
		buf[ret] = '\0';

	return ret;
}

/*
 *  stress_is_prime64()
 *      return true if 64 bit value n is prime
 *      http://en.wikipedia.org/wiki/Primality_test
 */
bool stress_is_prime64(const uint64_t n)
{
	register uint64_t i, max;
	double max_d;

	if (n <= 3)
		return n >= 2;
	if ((n % 2 == 0) || (n % 3 == 0))
		return false;
	max_d = 1.0 + sqrt((double)n);
	max = (uint64_t)max_d;
	for (i = 5; i < max; i+= 6)
		if ((n % i == 0) || (n % (i + 2) == 0))
			return false;
	return true;
}

/*
 *  stress_get_prime64()
 *	find a prime that is not a multiple of n,
 *	used for file name striding
 */
uint64_t stress_get_prime64(const uint64_t n)
{
	static uint p = 1009;

	if (n != p)
		return p;

	/* Search for next prime.. */
	for (;;) {
		p += 2;

		if ((n % p) && stress_is_prime64(p))
			return p;
	}
}

/*
 *  stress_get_max_file_limit()
 *	get max number of files that the current
 *	process can open not counting the files that
 *	may already been opened.
 */
size_t stress_get_max_file_limit(void)
{
#if defined(RLIMIT_NOFILE)
	struct rlimit rlim;
#endif
	size_t max_rlim = SIZE_MAX;
	size_t max_sysconf;

#if defined(RLIMIT_NOFILE)
	if (!getrlimit(RLIMIT_NOFILE, &rlim))
		max_rlim = (size_t)rlim.rlim_cur;
#endif
#if defined(_SC_OPEN_MAX)
	{
		const long open_max = sysconf(_SC_OPEN_MAX);

		max_sysconf = (open_max > 0) ? (size_t)open_max : SIZE_MAX;
	}
#else
	max_sysconf = SIZE_MAX;
#endif
	/* return the lowest of these two */
	return STRESS_MINIMUM(max_rlim, max_sysconf);
}

/*
 *  stress_get_file_limit()
 *	get max number of files that the current
 *	process can open excluding currently opened
 *	files.
 */
size_t stress_get_file_limit(void)
{
	struct rlimit rlim;
	size_t i, last_opened, opened = 0, max = 65536;	/* initial guess */

	if (!getrlimit(RLIMIT_NOFILE, &rlim))
		max = (size_t)rlim.rlim_cur;

	last_opened = 0;

	/* Determine max number of free file descriptors we have */
	for (i = 0; i < max; i++) {
		if (fcntl((int)i, F_GETFL) > -1) {
			opened++;
			last_opened = i;
		} else {
			/*
			 *  Hack: Over 250 contiguously closed files
			 *  most probably indicates we're at the point
			 *  were no more opened file descriptors are
			 *  going to be found, so bail out rather then
			 *  scanning for any more opened files
			 */
			if (i - last_opened > 250)
				break;
		}
	}
	return max - opened;
}

/*
 *  stress_get_bad_fd()
 *	return a fd that will produce -EINVAL when using it
 *	either because it is not open or it is just out of range
 */
int stress_get_bad_fd(void)
{
#if defined(RLIMIT_NOFILE) &&	\
    defined(F_GETFL)
	struct rlimit rlim;

	(void)memset(&rlim, 0, sizeof(rlim));

	if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
		if (rlim.rlim_cur < INT_MAX - 1) {
			if (fcntl((int)rlim.rlim_cur, F_GETFL) == -1) {
				return (int)rlim.rlim_cur + 1;
			}
		}
	}
#elif defined(F_GETFL)
	int i;

	for (i = 2048; i > fileno(stdout); i--) {
		if (fcntl((int)i, F_GETFL) == -1)
			return i;
	}
#endif
	return -1;
}

/*
 *  stress_sigaltstack()
 *	attempt to set up an alternative signal stack
 *	  stack - must be at least MINSIGSTKSZ
 *	  size  - size of stack (- STACK_ALIGNMENT)
 */
int stress_sigaltstack(void *stack, const size_t size)
{
#if defined(HAVE_SIGALTSTACK)
	stack_t ss;

	if (size < (size_t)STRESS_MINSIGSTKSZ) {
		pr_err("sigaltstack stack size %zu must be more than %zuK\n",
			size, (size_t)STRESS_MINSIGSTKSZ / 1024);
		return -1;
	}
	ss.ss_sp = (void *)stack;
	ss.ss_size = size;
	ss.ss_flags = 0;
	if (sigaltstack(&ss, NULL) < 0) {
		pr_fail("sigaltstack failed: errno=%d (%s)\n",
			errno, strerror(errno));
		return -1;
	}
#else
	(void)stack;
	(void)size;
#endif
	return 0;
}

/*
 *  stress_sighandler()
 *	set signal handler in generic way
 */
int stress_sighandler(
	const char *name,
	const int signum,
	void (*handler)(int),
	struct sigaction *orig_action)
{
	struct sigaction new_action;
#if defined(HAVE_SIGALTSTACK)
	{
		static uint8_t *stack = NULL;

		if (stack == NULL) {
			/* Allocate stack, we currently leak this */
			stack = (uint8_t *)mmap(NULL, STRESS_SIGSTKSZ, PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			if (stack == MAP_FAILED) {
				pr_inf("%s: sigaction %s: cannot allocated signal stack, "
					"errno = %d (%s)\n",
					name, stress_strsignal(signum),
					errno, strerror(errno));
				return -1;
			}
			if (stress_sigaltstack(stack, STRESS_SIGSTKSZ) < 0)
				return -1;
		}
	}
#endif
	(void)memset(&new_action, 0, sizeof new_action);
	new_action.sa_handler = handler;
	(void)sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = SA_ONSTACK;

	if (sigaction(signum, &new_action, orig_action) < 0) {
		pr_fail("%s: sigaction %s: errno=%d (%s)\n",
			name, stress_strsignal(signum), errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  stress_sighandler_default
 *	restore signal handler to default handler
 */
int stress_sighandler_default(const int signum)
{
	struct sigaction new_action;

	(void)memset(&new_action, 0, sizeof new_action);
	new_action.sa_handler = SIG_DFL;

	return sigaction(signum, &new_action, NULL);
}

/*
 *  stress_handle_stop_stressing()
 *	set flag to indicate to stressor to stop stressing
 */
void stress_handle_stop_stressing(int signum)
{
	(void)signum;

	keep_stressing_set_flag(false);
	/*
	 * Trigger another SIGARLM until stressor gets the message
	 * that it needs to terminate
	 */
	(void)alarm(1);
}

/*
 *  stress_sig_stop_stressing()
 *	install a handler that sets the global flag
 *	to indicate to a stressor to stop stressing
 */
int stress_sig_stop_stressing(const char *name, const int sig)
{
	return stress_sighandler(name, sig, stress_handle_stop_stressing, NULL);
}

/*
 *  stress_sigrestore()
 *	restore a handler
 */
int stress_sigrestore(
	const char *name,
	const int signum,
	struct sigaction *orig_action)
{
	if (sigaction(signum, orig_action, NULL) < 0) {
		pr_fail("%s: sigaction %s restore: errno=%d (%s)\n",
			name, stress_strsignal(signum), errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  stress_get_cpu()
 *	get cpu number that process is currently on
 */
unsigned int stress_get_cpu(void)
{
#if defined(HAVE_SCHED_GETCPU) &&	\
    !defined(__PPC64__) &&		\
    !defined(__s390x__)
	const int cpu = sched_getcpu();

	return (unsigned int)((cpu < 0) ? 0 : cpu);
#else
	return 0;
#endif
}

#define XSTRINGIFY(s) STRINGIFY(s)
#define STRINGIFY(s) #s

/*
 *  stress_get_compiler()
 *	return compiler info
 */
const char *stress_get_compiler(void)
{
#if defined(__clang_major__) &&	\
    defined(__clang_minor__)
	static const char cc[] = "clang " XSTRINGIFY(__clang_major__) "." XSTRINGIFY(__clang_minor__) "";
#elif defined(__GNUC__) &&	\
      defined(__GNUC_MINOR__)
	static const char cc[] = "gcc " XSTRINGIFY(__GNUC__) "." XSTRINGIFY(__GNUC_MINOR__) "";
#else
	static const char cc[] = "cc unknown";
#endif
	return cc;
}

/*
 *  stress_get_uname_info()
 *	return uname information
 */
const char *stress_get_uname_info(void)
{
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	struct utsname buf;

	if (!uname(&buf)) {
		static char str[sizeof(buf.machine) +
	                        sizeof(buf.sysname) +
				sizeof(buf.release) + 3];

		(void)snprintf(str, sizeof(str), "%s %s %s", buf.machine, buf.sysname, buf.release);
		return str;
	}
#endif
	return "unknown";
}

/*
 *  stress_not_implemented()
 *	report that a stressor is not implemented
 *	on a particular arch or kernel
 */
int stress_not_implemented(const stress_args_t *args)
{
	static const char msg[] = "this stressor is not implemented on "
				  "this system";
	if (args->instance == 0) {
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
		struct utsname buf;

		if (!uname(&buf)) {
			pr_inf_skip("%s: %s: %s %s\n",
				args->name, msg, stress_get_uname_info(),
				stress_get_compiler());
			return EXIT_NOT_IMPLEMENTED;
		}
#endif
		pr_inf_skip("%s: %s: %s\n",
			args->name, msg, stress_get_compiler());
	}
	return EXIT_NOT_IMPLEMENTED;
}

#if defined(F_SETPIPE_SZ)
/*
 *  stress_check_max_pipe_size()
 *	check if the given pipe size is allowed
 */
static inline int stress_check_max_pipe_size(
	const size_t sz,
	const size_t page_size)
{
	int fds[2];

	if (sz < page_size)
		return -1;

	if (pipe(fds) < 0)
		return -1;

	if (fcntl(fds[0], F_SETPIPE_SZ, sz) < 0)
		return -1;

	(void)close(fds[0]);
	(void)close(fds[1]);
	return 0;
}
#endif

/*
 *  stress_probe_max_pipe_size()
 *	determine the maximum allowed pipe size
 */
size_t stress_probe_max_pipe_size(void)
{
	static size_t max_pipe_size;

#if defined(F_SETPIPE_SZ)
	ssize_t ret;
	size_t i, prev_sz, sz, min, max;
	char buf[64];
	size_t page_size;
#endif
	/* Already determined? returned cached size */
	if (max_pipe_size)
		return max_pipe_size;

#if defined(F_SETPIPE_SZ)
	page_size = stress_get_pagesize();

	/*
	 *  Try and find maximum pipe size directly
	 */
	ret = system_read("/proc/sys/fs/pipe-max-size", buf, sizeof(buf));
	if (ret > 0) {
		if (sscanf(buf, "%zd", &sz) == 1)
			if (!stress_check_max_pipe_size(sz, page_size))
				goto ret;
	}

	/*
	 *  Need to find size by binary chop probing
	 */
	min = page_size;
	max = INT_MAX;
	prev_sz = 0;
	sz = 0;
	for (i = 0; i < 64; i++) {
		sz = min + (max - min) / 2;
		if (prev_sz == sz)
			return sz;
		prev_sz = sz;
		if (stress_check_max_pipe_size(sz, page_size) == 0) {
			min = sz;
		} else {
			max = sz;
		}
	}
ret:
	max_pipe_size = sz;
#else
	max_pipe_size = stress_get_pagesize();
#endif
	return max_pipe_size;
}

/*
 *  stress_align_address
 *	align address to alignment, alignment MUST be a power of 2
 */
void *stress_align_address(const void *addr, const size_t alignment)
{
	const uintptr_t uintptr =
		((uintptr_t)addr + alignment) & ~(alignment - 1);

	return (void *)uintptr;
}

/*
 *  stress_sigalrm_pending()
 *	return true if SIGALRM is pending
 */
bool stress_sigalrm_pending(void)
{
	sigset_t set;

	(void)sigemptyset(&set);
	(void)sigpending(&set);
	return sigismember(&set, SIGALRM);

}

/*
 *  stress_uint64_to_str()
 *	turn 64 bit size to human readable string
 */
char *stress_uint64_to_str(char *str, size_t len, const uint64_t val)
{
	typedef struct {
		uint64_t size;
		char *suffix;
	} stress_size_info_t;

	static const stress_size_info_t size_info[] = {
		{ EB, "E" },
		{ PB, "P" },
		{ TB, "T" },
		{ GB, "G" },
		{ MB, "M" },
		{ KB, "K" },
	};
	size_t i;
	char *suffix = "";
	uint64_t scale = 1;

	for (i = 0; i < SIZEOF_ARRAY(size_info); i++) {
		uint64_t scaled = val / size_info[i].size;

		if ((scaled >= 1) && (scaled < 1024)) {
			suffix = size_info[i].suffix;
			scale = size_info[i].size;
			break;
		}
	}

	(void)snprintf(str, len, "%.1f%s", (double)val / (double)scale, suffix);

	return str;
}

/*
 *  stress_check_root()
 *	returns true if root
 */
static bool stress_check_root(void)
{
	return (geteuid() == 0);
}

#if defined(HAVE_SYS_CAPABILITY_H)
/*
 *  stress_check_capability()
 *	returns true if process has the given capability,
 *	if capability is SHIM_CAP_IS_ROOT then just check if process is
 *	root.
 */
bool stress_check_capability(const int capability)
{
	int ret;
	struct __user_cap_header_struct uch;
	struct __user_cap_data_struct ucd[_LINUX_CAPABILITY_U32S_3];
	uint32_t mask;
	size_t idx;

	if (capability == SHIM_CAP_IS_ROOT)
		return stress_check_root();

	(void)memset(&uch, 0, sizeof uch);
	(void)memset(ucd, 0, sizeof ucd);

	uch.version = _LINUX_CAPABILITY_VERSION_3;
	uch.pid = getpid();

	ret = capget(&uch, ucd);
	if (ret < 0)
		return stress_check_root();

	idx = (size_t)CAP_TO_INDEX(capability);
	mask = CAP_TO_MASK(capability);

	return (ucd[idx].permitted &= mask) ? true : false;
}
#else
bool stress_check_capability(const int capability)
{
	(void)capability;

	return stress_check_root();
}
#endif

#if defined(HAVE_SYS_CAPABILITY_H)
int stress_drop_capabilities(const char *name)
{
	int ret;
	uint32_t i;
	struct __user_cap_header_struct uch;
	struct __user_cap_data_struct ucd[_LINUX_CAPABILITY_U32S_3];

	(void)memset(&uch, 0, sizeof uch);
	(void)memset(ucd, 0, sizeof ucd);

	uch.version = _LINUX_CAPABILITY_VERSION_3;
	uch.pid = getpid();

	ret = capget(&uch, ucd);
	if (ret < 0) {
		pr_fail("%s: capget on pid %d failed: errno=%d (%s)\n",
			name, uch.pid, errno, strerror(errno));
		return -1;
	}

	/*
	 *  We could just memset ucd to zero, but
	 *  lets explicitly set all the capability
	 *  bits to zero to show the intent
	 */
	for (i = 0; i <= CAP_LAST_CAP; i++) {
		uint32_t idx = CAP_TO_INDEX(i);
		uint32_t mask = CAP_TO_MASK(i);

		ucd[idx].inheritable &= ~mask;
		ucd[idx].permitted &= ~mask;
		ucd[idx].effective &= ~mask;
	}

	ret = capset(&uch, ucd);
	if (ret < 0) {
		pr_fail("%s: capset on pid %d failed: errno=%d (%s)\n",
			name, uch.pid, errno, strerror(errno));
		return -1;
	}
#if defined(HAVE_PRCTL) &&		\
    defined(HAVE_SYS_PRCTL_H) &&	\
    defined(PR_SET_NO_NEW_PRIVS)
	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	if (ret < 0) {
		pr_inf("%s: prctl PR_SET_NO_NEW_PRIVS on pid %d failed: "
			"errno=%d (%s)\n",
			name, uch.pid, errno, strerror(errno));
		return -1;
	}
#endif
	return 0;
}
#else
int stress_drop_capabilities(const char *name)
{
	(void)name;

	return 0;
}
#endif

/*
 *  stress_is_dot_filename()
 *	is filename "." or ".."
 */
bool stress_is_dot_filename(const char *name)
{
	if (!strcmp(name, "."))
		return true;
	if (!strcmp(name, ".."))
		return true;
	return false;
}

/*
 *  stress_const_optdup(const char *opt)
 *	duplicate a modifiable copy of a const option string opt
 */
char *stress_const_optdup(const char *opt)
{
	char *str = strdup(opt);

	if (!str)
		(void)fprintf(stderr, "out of memory duplicating option '%s'\n", opt);

	return str;
}

/*
 *  stress_text_addr()
 *	return length and start/end addresses of text segment
 */
size_t stress_text_addr(char **start, char **end)
{
#if defined(HAVE_EXECUTABLE_START)
	extern char __executable_start;
	intptr_t text_start = (intptr_t)&__executable_start;
#elif defined(__APPLE__)
        extern char _mh_execute_header;
        intptr_t text_start = (intptr_t)&_mh_execute_header;
#elif defined(__OpenBSD__)
        extern char _start[];
        intptr_t text_start = (intptr_t)&_start[0];
#elif defined(__TINYC__)
        extern char _start;
        intptr_t text_start = (intptr_t)&_start;
#else
        extern char _start;
        intptr_t text_start = (intptr_t)&_start;
#endif

#if defined(__APPLE__)
        extern void *get_etext(void);
        intptr_t text_end = (intptr_t)get_etext();
#elif defined(__TINYC__)
        extern char _etext;
        intptr_t text_end = (intptr_t)&_etext;
#else
        extern char etext;
        intptr_t text_end = (intptr_t)&etext;
#endif
        const size_t text_len = (size_t)(text_end - text_start);

	if ((start == NULL) || (end == NULL) || (text_start >= text_end))
		return 0;

	*start = (char *)text_start;
	*end = (char *)text_end;

	return text_len;
}

/*
 *  stress_is_dev_tty()
 *	return true if fd is on a /dev/ttyN device. If it can't
 *	be determined than default to assuming it is.
 */
bool stress_is_dev_tty(const int fd)
{
#if defined(HAVE_TTYNAME)
	const char *name = ttyname(fd);

	if (!name)
		return true;
	return !strncmp("/dev/tty", name, 8);
#else
	(void)fd;

	/* Assume it is */
	return true;
#endif
}

/*
 *  stress_dirent_list_free()
 *	free dirent list
 */
void stress_dirent_list_free(struct dirent **dlist, const int n)
{
	if (dlist) {
		int i;

		for (i = 0; i < n; i++) {
			if (dlist[i])
				free(dlist[i]);
		}
		free(dlist);
	}
}

/*
 *  stress_dirent_list_prune()
 *	remove . and .. files from directory list
 */
int stress_dirent_list_prune(struct dirent **dlist, const int n)
{
	int i, j;

	for (i = 0, j = 0; i < n; i++) {
		if (dlist[i]) {
			if (stress_is_dot_filename(dlist[i]->d_name)) {
				free(dlist[i]);
				dlist[i] = NULL;
			} else {
				dlist[j] = dlist[i];
				j++;
			}
		}
	}
	return j;
}

/*
 *  stress_warn_once_hash()
 *	computes a hash for a filename and a line and stores it,
 *	returns true if this is the first time this has been
 *	called for that specific filename and line
 *
 *	Without libpthread this is potentially racy.
 */
bool stress_warn_once_hash(const char *filename, const int line)
{
	uint32_t free_slot, i, j, h = (stress_hash_pjw(filename) + (uint32_t)line);
	bool not_warned_yet = true;
#if defined(HAVE_LIB_PTHREAD)
        int ret;
#endif
	if (!g_shared)
		return true;

#if defined(HAVE_LIB_PTHREAD)
        ret = shim_pthread_spin_lock(&g_shared->warn_once.lock);
#endif
	free_slot = STRESS_WARN_HASH_MAX;

	/*
	 * Ensure hash is never zero so that it does not
	 * match and empty slot value of zero
	 */
	if (h == 0)
		h += STRESS_WARN_HASH_MAX;

	j = h % STRESS_WARN_HASH_MAX;
	for (i = 0; i < STRESS_WARN_HASH_MAX; i++) {
		if (g_shared->warn_once.hash[j] == h) {
			not_warned_yet = false;
			goto unlock;
		}
		if ((free_slot == STRESS_WARN_HASH_MAX) &&
		    (g_shared->warn_once.hash[j] == 0)) {
			free_slot = j;
		}
		j = (j + 1) % STRESS_WARN_HASH_MAX;
	}
	if (free_slot != STRESS_WARN_HASH_MAX) {
		g_shared->warn_once.hash[free_slot] = h;
	}
unlock:
#if defined(HAVE_LIB_PTHREAD)
        if (!ret)
                shim_pthread_spin_unlock(&g_shared->warn_once.lock);
#endif
        return not_warned_yet;
}

/*
 *  stress_ipv4_checksum()
 *	ipv4 data checksum
 */
uint16_t HOT OPTIMIZE3 stress_ipv4_checksum(uint16_t *ptr, const size_t sz)
{
	register uint32_t sum = 0;
	register size_t n = sz;

	while (n > 1) {
		sum += *ptr++;
		n -= 2;
	}

	if (n)
		sum += *(uint8_t*)ptr;
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);

	return (uint16_t)~sum;
}

#if defined(HAVE_SETPWENT) &&	\
    defined(HAVE_GETPWENT) &&	\
    defined(HAVE_ENDPWENT) &&	\
    !defined(BUILD_STATIC)
static int stress_uid_comp(const void *p1, const void *p2)
{
	const uid_t *uid1 = (const uid_t *)p1;
	const uid_t *uid2 = (const uid_t *)p2;

	if (*uid1 > *uid2)
		return 1;
	else if (*uid1 < *uid2)
		return -1;
	else
		return 0;
}

/*
 *  stress_get_unused_uid()
 *	find the lowest free unused UID greater than 250,
 *	returns -1 if it can't find one and uid is set to 0;
 *      if successful it returns 0 and sets uid to the free uid.
 *
 *	This also caches the uid so this can be called
 *	frequently. If the cached uid is in use it will
 *	perform the expensive lookup again.
 */
int stress_get_unused_uid(uid_t *uid)
{
	static uid_t cached_uid = 0;
	uid_t *uids;

	*uid = 0;

	/*
	 *  If we have a cached unused uid and it's no longer
	 *  unused then force a rescan for a new one
	 */
	if ((cached_uid != 0) && (getpwuid(cached_uid) != NULL))
		cached_uid = 0;

	if (cached_uid == 0) {
		struct passwd *pw;
		size_t i, n;

		setpwent();
		for (n = 0; getpwent() != NULL; n++) {
		}
		endpwent();

		uids = calloc(n, sizeof(*uids));
		if (!uids)
			return -1;

		setpwent();
		for (i = 0; i < n && (pw = getpwent()) != NULL; i++) {
			uids[i] = pw->pw_uid;
		}
		endpwent();
		n = i;

		qsort(uids, n, sizeof(*uids), stress_uid_comp);

		/* Look for a suitable gap from uid 250 upwards */
		for (i = 0; i < n - 1; i++) {
			/*
			 *  Add a large gap in case new uids
			 *  are added to reduce free uid race window
			 */
			const uid_t uid_try = uids[i] + 250;

			if (uids[i + 1] > uid_try) {
				if (getpwuid(uid_try) == NULL) {
					cached_uid = uid_try;
					break;
				}
			}
		}
		free(uids);
	}

	/*
	 *  Not found?
	 */
	if (cached_uid == 0)
		return -1;

	*uid = cached_uid;

	return 0;
}
#else
int stress_get_unused_uid(uid_t *uid)
{
	*uid = 0;

	return -1;
}
#endif

/*
 *  stress_read_buffer()
 *	In addition to read() this function makes sure all bytes have been
 *	written. You're also able to ignore EINTR interrupts which could happen
 *	on alarm() in the parent process.
 */
ssize_t stress_read_buffer(int fd, void* buffer, ssize_t size, bool ignore_int)
{
	ssize_t rbytes = 0, ret;

	do {
		char *ptr = ((char *)buffer) + rbytes;
ignore_eintr:

		ret = read(fd, (void *)ptr, (size_t)(size - rbytes));
		if (ignore_int && (ret < 0) && (errno == EINTR))
			goto ignore_eintr;
		if (ret > 0)
			rbytes += ret;
	} while (ret > 0 && (rbytes != size));

	pr_dbg_v("stress_read_buffer: size=%ld read=%ld sz2=%ld\n", size, rbytes, ret);

	return (ret <= 0)? ret : rbytes;
}

/*
 *  stress_write_buffer()
 *	In addition to write() this function makes sure all bytes have been
 *	written. You're also able to ignore EINTR interrupts which could happen
 *	on alarm() in the parent process.
 */
ssize_t stress_write_buffer(int fd, void* buffer, ssize_t size, bool ignore_int)
{
	ssize_t wbytes = 0, ret;

	do {
		char *ptr = ((char *)buffer) + wbytes;
ignore_eintr:
		ret = write(fd, (void *)ptr, (size_t)(size - wbytes));
		/* retry if interrupted */
		if (ignore_int && (ret < 0) && (errno == EINTR))
			goto ignore_eintr;
		if (ret > 0)
			wbytes += ret;
	} while (ret > 0 && (wbytes != size));

	pr_dbg_v("stress_write_buffer: size=%ld written=%ld sz2=%ld\n", size, wbytes, ret);

	return (ret <= 0)? ret : wbytes;
}

/*
 *  stress_kernel_release()
 *	turn release major.minor.patchlevel triplet into base 100 value
 */
int stress_kernel_release(const int major, const int minor, const int patchlevel)
{
	return (major * 10000) + (minor * 100) + patchlevel;
}

/*
 *  stress_get_kernel_release()
 *	return kernel release number in base 100, e.g.
 *	 4.15.2 -> 401502, return -1 if failed.
 */
int stress_get_kernel_release(void)
{
#if defined(HAVE_UNAME)
	struct utsname buf;
	int major = 0, minor = 0, patchlevel = 0;

	if (uname(&buf) < 0)
		return -1;

	if (sscanf(buf.release, "%d.%d.%d\n", &major, &minor, &patchlevel) < 1)
		return -1;

	return stress_kernel_release(major, minor, patchlevel);
#else
	return -1;
#endif
}

/*
 *  stress_get_unused_pid_racy()
 *	try to find an unused pid. This is racy and may actually
 *	return pid that is unused at test time but will become
 *	used by the time the pid is accessed.
 */
pid_t stress_get_unused_pid_racy(const bool fork_test)
{
	char buf[64];
#if defined(PID_MAX_LIMIT)
	pid_t max_pid = PID_MAX_LIMIT;
#elif defined(PID_MAX)
	pid_t max_pid = PID_MAX;
#elif defined(PID_MAX_DEFAULT)
	pid_t max_pid = PID_MAX_DEFAULT;
#else
	pid_t max_pid = 32767;
#endif
	int i;
	pid_t pid;
	uint32_t n;

	(void)memset(buf, 0, sizeof(buf));
	if (system_read("/proc/sys/kernel/pid_max", buf, sizeof(buf) - 1) > 0) {
		max_pid = atoi(buf);
	}
	if (max_pid < 1024)
		max_pid = 1024;

	/*
	 *  Create a child, terminate it, use this pid as an unused
	 *  pid. Slow but should be OK if system doesn't recycle PIDs
	 *  quickly.
	 */
	if (fork_test) {
		pid = fork();
		if (pid == 0) {
			_exit(0);
		} else if (pid > 0) {
			int status, ret;

			ret = waitpid(pid, &status, 0);
			if ((ret == pid) &&
			    ((kill(pid, 0) < 0) && (errno == ESRCH))) {
				return pid;
			}
		}
	}

	/*
	 *  Make a random PID guess.
	 */
	n = (uint32_t)max_pid - 1023;
	for (i = 0; i < 20; i++) {
		pid = (pid_t)(stress_mwc32() % n) + 1023;

		if ((kill(pid, 0) < 0) && (errno == ESRCH))
			return pid;
	}

	/*
	 *  Give up.
	 */
	return max_pid;
}

/*
 *  stress_read_fdinfo()
 *	read the fdinfo for a specific pid's fd, Linux only
 */
int stress_read_fdinfo(const pid_t pid, const int fd)
{
#if defined(__linux__)
	char path[PATH_MAX];
	char buf[4096];

	(void)snprintf(path, sizeof(path), "/proc/%d/fdinfo/%d",
                (int)pid, fd);

        return (int)system_read(path, buf, sizeof(buf));
#else
	(void)pid;
	(void)fd;

	return 0;
#endif
}

/*
 *  stress_hostname_length()
 *	return the maximum allowed hostname length
 */
size_t stress_hostname_length(void)
{
#if defined(HOST_NAME_MAX)
	return HOST_NAME_MAX + 1;
#elif defined(HAVE_UNAME) && \
      defined(HAVE_SYS_UTSNAME_H)
	struct utsname uts;

	return sizeof(uts.nodename);	/* Linux */
#else
	return 255 + 1;			/* SUSv2 */
#endif
}

/*
 *  stress_min_aux_sig_stack_size()
 *	For ARM we should check AT_MINSIGSTKSZ as this
 *	also includes SVE register saving overhead
 *	https://blog.linuxplumbersconf.org/2017/ocw/system/presentations/4671/original/plumbers-dm-2017.pdf
 */
static inline long stress_min_aux_sig_stack_size(void)
{
#if defined(HAVE_GETAUXVAL) &&	\
    defined(AT_MINSIGSTKSZ)
	long sz = getauxval(AT_MINSIGSTKSZ);

	if (sz > 0)
		return sz;
#endif
	return -1;
}

/*
 *  stress_sig_stack_size()
 *	wrapper for STRESS_SIGSTKSZ, try and find
 *	stack size required
 */
size_t stress_sig_stack_size(void)
{
	static long sz = -1, min;

	/* return cached copy */
	if (sz > 0)
		return sz;

	min = stress_min_aux_sig_stack_size();
#if defined(_SC_SIGSTKSZ)
	sz = sysconf(_SC_SIGSTKSZ);
	if (sz > min)
		min = sz;
#endif
#if defined(SIGSTKSZ)
	if (SIGSTKSZ > min) {
		/* SIGSTKSZ may be sysconf(_SC_SIGSTKSZ) */
		min = SIGSTKSZ;
		if (min < 0)
			min = 8192;
	}
#else
	if (8192 > min)
		min = 8192;
#endif
	sz = min;

	return (size_t)sz;
}

/*
 *  stress_min_sig_stack_size()
 *	wrapper for STRESS_MINSIGSTKSZ
 */
size_t stress_min_sig_stack_size(void)
{
	static long sz = -1, min;

	/* return cached copy */
	if (sz > 0)
		return sz;

	min = stress_min_aux_sig_stack_size();
#if defined(_SC_MINSIGSTKSZ)
	sz = sysconf(_SC_MINSIGSTKSZ);
	if (sz > min)
		min = sz;
#endif
#if defined(SIGSTKSZ)
	if (SIGSTKSZ > min) {
		/* SIGSTKSZ may be sysconf(_SC_SIGSTKSZ) */
		min = SIGSTKSZ;
		if (min < 0)
			min = 8192;
	}
#else
	if (8192 > min)
		min = 8192;
#endif
	sz = min;

	return (size_t)sz;
}

/*
 *  stress_min_pthread_stack_size()
 *	return the minimum size of stack for a pthread
 */
size_t stress_min_pthread_stack_size(void)
{
	static long sz = -1, min;

	/* return cached copy */
	if (sz > 0)
		return sz;

	min = stress_min_aux_sig_stack_size();
#if defined(__SC_THREAD_STACK_MIN_VALUE)
	sz = sysconf(__SC_THREAD_STACK_MIN_VALUE);
	if (sz > min)
		min = sz;
#endif
#if defined(_SC_THREAD_STACK_MIN_VALUE)
	sz = sysconf(_SC_THREAD_STACK_MIN_VALUE);
	if (sz > min)
		min = sz;
#endif
#if defined(PTHREAD_STACK_MIN)
	if (PTHREAD_STACK_MIN > min)
		min = PTHREAD_STACK_MIN;
#endif
	if (8192 > min)
		min = 8192;

	sz = min;

	return (size_t)sz;
}

/*
 *  stress_sig_handler_exit()
 *	signal handler that exits a process via _exit(0) for
 *	immediate dead stop termination.
 */
void NORETURN MLOCKED_TEXT stress_sig_handler_exit(int signum)
{
	(void)signum;

	_exit(0);
}

/*
 *  __stack_chk_fail()
 *	override stack smashing callback
 */
#if (defined(__GNUC__) || defined(__clang__)) &&	\
    defined(HAVE_WEAK_ATTRIBUTE)
extern void __stack_chk_fail(void);

NORETURN WEAK void __stack_chk_fail(void)
{
	if (stress_stack_check_flag) {
		fprintf(stderr, "Stack overflow detected! Aborting stress-ng.\n");
		fflush(stderr);
		abort();
	}
	/* silently exit */
	_exit(0);
}
#endif

/*
 *  stress_set_stack_smash_check_flag()
 *	set flag, true = report flag, false = silently ignore
 */
void stress_set_stack_smash_check_flag(const bool flag)
{
	stress_stack_check_flag = flag;
}

int stress_tty_width(void)
{
	const int max_width = 80;
#if defined(HAVE_WINSIZE) &&	\
    defined(TIOCGWINSZ)
	struct winsize ws;
	int ret;

	ret = ioctl(fileno(stdout), TIOCGWINSZ, &ws);
	if (ret < 0)
		return max_width;
	ret = (int)ws.ws_col;
	if ((ret < 0) || (ret > 1024))
		return max_width;
	return ret;
#else
	return max_width;
#endif
}

/*
 *  stress_get_extents()
 *	try to determine number extents in a file
 */
size_t stress_get_extents(const int fd)
{
#if defined(FS_IOC_FIEMAP) &&	\
    defined(HAVE_LINUX_FIEMAP_H)
	struct fiemap fiemap;

	(void)memset(&fiemap, 0, sizeof(fiemap));
	fiemap.fm_length = ~0UL;

	/* Find out how many extents there are */
	if (ioctl(fd, FS_IOC_FIEMAP, &fiemap) < 0)
		return 0;

	return fiemap.fm_mapped_extents;
#else
	(void)fd;

	return 0;
#endif
}

/*
 *  stress_redo_fork()
 *	check fork errno (in err) and return true if
 *	an immediate fork can be retried due to known
 *	error cases that are retryable. Also force a
 *	scheduling yield.
 */
bool stress_redo_fork(const int err)
{
	if (keep_stressing_flag() &&
	    ((err == EAGAIN) || (err == EINTR) || (err == ENOMEM))) {
		(void)shim_sched_yield();
		return true;
	}
	return false;
}
