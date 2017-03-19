/*
 * Copyright (C) 2014-2017 Canonical, Ltd.
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

#include <libgen.h>
#include <math.h>
#if defined(__linux__)
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/prctl.h>
#endif
#if !defined(__NetBSD__) && !defined(__OpenBSD__) && !defined(__FreeBSD__)
#include <sys/utsname.h>
#endif
#include <sys/statvfs.h>

#if !defined(PR_SET_DISABLE)
#define SUID_DUMP_DISABLE	(0)       /* No setuid dumping */
#endif
#if !defined(SUID_DUMP_USER)
#define SUID_DUMP_USER		(1)       /* Dump as user of process */
#endif

#if defined(__linux__) && defined(PRCTL_TIMER_SLACK)
static unsigned long timer_slack = 0;
#endif

static const char *stress_temp_path = ".";

/*
 *  stress_get_pagesize()
 *	get pagesize
 */
size_t stress_get_pagesize(void)
{
#if defined(_SC_PAGESIZE)
	long sz;
#endif
	static size_t page_size = 0;

	if (page_size > 0)
		return page_size;

#if defined(_SC_PAGESIZE)
	sz = sysconf(_SC_PAGESIZE);
	page_size = (sz <= 0) ? PAGE_4K : (size_t)sz;
#else
	page_size = PAGE_4K;
#endif

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
	static uint32_t ticks_per_second = 0;

	if (ticks_per_second > 0)
		return ticks_per_second;

#if defined(_SC_CLK_TCK)
	ticks_per_second = sysconf(_SC_CLK_TCK);
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
	size_t *totalmem)
{
#if defined(__linux__)
	struct sysinfo info;
	FILE *fp;
#endif
	*shmall = 0;
	*freemem = 0;
	*totalmem = 0;

#if defined(__linux__)
	if (sysinfo(&info) == 0) {
		*freemem = info.freeram * info.mem_unit;
		*totalmem = info.totalram * info.mem_unit;
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

	phys_pages = sysconf(STRESS_SC_PAGES);
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
	int rc;
	struct statvfs buf;
	fsblkcnt_t blocks, max_blocks;

	if (!stress_temp_path)
		return 0;

	rc = statvfs(stress_temp_path, &buf);
	if (rc < 0)
		return 0;

	max_blocks = (~(fsblkcnt_t)0) / buf.f_bsize;
	blocks = buf.f_bavail;

	if (blocks > max_blocks)
		blocks = max_blocks;

	(void)printf("stress_get_filesystem_size: %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
		(uint64_t)buf.f_bsize, (uint64_t)blocks,
		(uint64_t)buf.f_bsize * blocks);

	return (uint64_t)buf.f_bsize * blocks;
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
#if NEED_GLIBC(2, 2, 0)
	int rc;
	double loadavg[3];

	rc = getloadavg(loadavg, 3);
	if (rc < 0)
		goto fail;

	*min1 = loadavg[0];
	*min5 = loadavg[1];
	*min15 = loadavg[2];

	return 0;
fail:
#elif defined(__linux__)
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
#if defined(__linux__) && defined(PR_SET_PDEATHSIG)
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
	struct rlimit lim = { 0, 0 };

	(void)setrlimit(RLIMIT_CORE, &lim);
#endif

	/*
	 *  changing PR_SET_DUMPABLE also affects the
	 *  oom adjust capability, so for now, we disable
	 *  this as I'd rather have a oom'able process when
	 *  memory gets contrained. Don't enable this
	 *  unless one checks that processes able oomable!
	 */
#if 0 && defined(__linux__) && defined(PR_SET_DUMPABLE)
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
void stress_set_timer_slack_ns(const char *optarg)
{
#if defined(__linux__) && defined(PRCTL_TIMER_SLACK)
	timer_slack = get_uint32(optarg);
#else
	(void)optarg;
#endif
}

/*
 *  stress_set_timer_slack()
 *	set timer slack
 */
void stress_set_timer_slack(void)
{
#if defined(__linux__) && defined(PRCTL_TIMER_SLACK)
	(void)prctl(PR_SET_TIMERSLACK, timer_slack);
#endif
}

/*
 *  set_proc_name()
 *	Set process name, we don't care if it fails
 */
void set_proc_name(const char *name)
{
#if defined(__linux__) && defined(PR_SET_NAME)
	if (!(g_opt_flags & OPT_FLAGS_KEEP_NAME))
		(void)prctl(PR_SET_NAME, name);
#else
	(void)name;	/* No-op */
#endif
}

/*
 *  munge_underscore()
 *	turn '_' to '-' in strings
 */
char *munge_underscore(const char *str)
{
	static char munged[128];
	char *dst;
	const char *src;
	size_t str_len = strlen(str);
	ssize_t len = STRESS_MINIMUM(str_len, sizeof(munged) - 1);

	for (src = str, dst = munged; *src && (dst - munged) < len; src++)
		*dst++ = (*src == '_' ? '-' : *src);

	*dst = '\0';

	return munged;
}

static ssize_t __stress_get_stack_direction(const uint8_t *val1)
{
	const uint8_t val2;
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
	return __stress_get_stack_direction(&val1);
}

/*
 *  uint64_zero()
 *	return uint64 zero in way that force less smart
 *	static analysers to realise we are doing this
 *	to force a division by zero. I'd like to have
 *	a better solution than this ghastly way.
 */
uint64_t uint64_zero(void)
{
	return 0ULL;
}

/*
 *  stress_set_temp_path()
 *	set temporary file path, default
 *	is . - current dir
 */
int stress_set_temp_path(const char *path)
{
	stress_temp_path = path;

	if (access(path, R_OK | W_OK) < 0) {
		(void)fprintf(stderr, "temp-path '%s' must be readable "
			"and writeable\n", path);
		return -1;
	}

	return 0;
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
	return snprintf(path, len, "%s/tmp-%s-%d-%"
		PRIu32 "/%s-%d-%"
		PRIu32 "-%" PRIu64,
		stress_temp_path,
		name, (int)pid, instance,
		name, (int)pid, instance, magic);
}

/*
 *  stress_temp_filename_args()
 *      construct a temp filename using info from args
 */
int stress_temp_filename_args(
	const args_t *args,
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
	return snprintf(path, len, "%s/tmp-%s-%d-%" PRIu32,
		stress_temp_path, name, (int)pid, instance);
}

/*
 *  stress_temp_dir_args()
 *	create a temporary directory name using info from args
 */
int stress_temp_dir_args(
	const args_t *args,
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
		pr_fail("%s: mkdir failed, errno=%d (%s)\n",
			name, errno, strerror(errno));
	}

	return ret;
}

/*
 *   stress_temp_dir_mk_args()
 *	create a temporary director using info from args
 */
int stress_temp_dir_mk_args(const args_t *args)
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
		pr_fail("%s: rmdir failed, errno=%d (%s)\n",
			name, errno, strerror(errno));
	}

	return ret;
}

/*
 *  stress_temp_dir_rm_args()
 *	remove a temporary directory using info from args
 */
int stress_temp_dir_rm_args(const args_t *args)
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
		pr_dbg("Cannot determine current working directory\n");
		return;
	}
	if (access(path, R_OK | W_OK)) {
		pr_inf("Working directory %s is not read/writeable, "
			"some I/O tests may fail\n", path);
		return;
	}
}

/*
 *  stress_strsignal()
 *	signum to human readable string
 */
const char *stress_strsignal(const int signum)
{
	static char buffer[128];
	char *str = NULL;

#if defined(NSIG)
	if ((signum >= 0) && (signum < NSIG))
		str = strsignal(signum);
#elif defined(_NSIG)
	if ((signum >= 0) && (signum < N_SIG))
		str = strsignal(signum);
#endif
	if (str) {
		(void)snprintf(buffer, sizeof(buffer), "signal %d (%s)",
			signum, str);
	} else {
		(void)snprintf(buffer, sizeof(buffer), "signal %d",
			signum);
	}
	return buffer;
}


/*
 *  stress_strnrnd()
 *	fill string with random chars
 */
void stress_strnrnd(char *str, const size_t len)
{
	char *end = str + len;

	while (str < end - 1)
		*str++ = (mwc8() % 26) + 'a';

	*str = '\0';
}

/*
 *  pr_yaml_runinfo()
 *	log info about the system we are running stress-ng on
 */
void pr_yaml_runinfo(FILE *yaml)
{
#if defined(__linux__)
	struct utsname uts;
	struct sysinfo info;
#endif
	time_t t;
	struct tm *tm = NULL;
	char hostname[128];
	char *user = shim_getlogin();

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
#if defined(__linux__)
	if (uname(&uts) == 0) {
		pr_yaml(yaml, "      sysname: %s\n", uts.sysname);
		pr_yaml(yaml, "      nodename: %s\n", uts.nodename);
		pr_yaml(yaml, "      release: %s\n", uts.release);
		pr_yaml(yaml, "      version: %s\n", uts.version);
		pr_yaml(yaml, "      machine: %s\n", uts.machine);
	}
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
	cpus_t *cpu_caches;
	cpu_cache_t *cache = NULL;
	uint16_t max_cache_level = 0;
#endif

#if !defined(__linux__)
	g_shared->mem_cache_size = MEM_CACHE_SIZE;
#else
	cpu_caches = get_all_cpu_cache_details();
	if (!cpu_caches) {
		if (warn_once(WARN_ONCE_CACHE_DEFAULT))
			pr_inf("%s: using built-in defaults as unable to "
				"determine cache details\n", name);
		g_shared->mem_cache_size = MEM_CACHE_SIZE;
		goto init_done;
	}

	max_cache_level = get_max_cache_level(cpu_caches);

	if (g_shared->mem_cache_level > max_cache_level) {
		if (warn_once(WARN_ONCE_CACHE_REDUCED))
			pr_dbg("%s: reducing cache level from L%d (too high) "
				"to L%d\n", name,
				g_shared->mem_cache_level, max_cache_level);
		g_shared->mem_cache_level = max_cache_level;
	}

	cache = get_cpu_cache(cpu_caches, g_shared->mem_cache_level);
	if (!cache) {
		if (warn_once(WARN_ONCE_CACHE_NONE))
			pr_inf("%s: using built-in defaults as no suitable "
				"cache found\n", name);
		g_shared->mem_cache_size = MEM_CACHE_SIZE;
		goto init_done;
	}

	if (g_shared->mem_cache_ways > 0) {
		uint64_t way_size;

		if (g_shared->mem_cache_ways > cache->ways) {
			if (warn_once(WARN_ONCE_CACHE_WAY))
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
		if (warn_once(WARN_ONCE_CACHE_DEFAULT))
			pr_inf("%s: using built-in defaults as "
				"unable to determine cache size\n", name);
		g_shared->mem_cache_size = MEM_CACHE_SIZE;
	}
init_done:
	free_cpu_caches(cpu_caches);
#endif
	g_shared->mem_cache = calloc(g_shared->mem_cache_size, 1);
	if (!g_shared->mem_cache) {
		pr_err("%s: failed to allocate shared cache buffer\n",
			name);
		return -1;
	}
	if (warn_once(WARN_ONCE_CACHE_SIZE))
		pr_dbg("%s: default cache size: %" PRIu64 "K\n",
			name, g_shared->mem_cache_size / 1024);

	return 0;
}

/*
 *  stress_cache_free()
 *	free shared cache buffer
 */
void stress_cache_free(void)
{
	free(g_shared->mem_cache);
}

/*
 *  system_write()
 *	write a buffer to a /sys or /proc entry
 */
int system_write(
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
int system_read(
	const char *path,
	char *buf,
	const size_t buf_len)
{
	int fd;
	ssize_t ret;

	memset(buf, 0, buf_len);

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;
	ret = read(fd, buf, buf_len);
	if (ret < 0)
		ret = -errno;
	(void)close(fd);

	return ret;
}


/*
 *  stress_is_prime64()
 *      return true if 64 bit value n is prime
 *      http://en.wikipedia.org/wiki/Primality_test
 */
static inline bool stress_is_prime64(const uint64_t n)
{
	register uint64_t i, max;

	if (n <= 3)
		return n >= 2;
	if ((n % 2 == 0) || (n % 3 == 0))
		return false;
	max = sqrt(n) + 1;
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
 *  stress_get_file_limit()
 *	get max number of files that the current
 *	process can open;
 */
size_t stress_get_file_limit(void)
{
	struct rlimit rlim;
	size_t i, opened = 0, max = 65536;	/* initial guess */

	if (!getrlimit(RLIMIT_NOFILE, &rlim))
		max = (size_t)rlim.rlim_cur;

	/* Determine max number of free file descriptors we have */
	for (i = 0; i < max; i++) {
		if (fcntl((int)i, F_GETFL) > -1)
			opened++;
	}
	return max - opened;
}

/*
 *  stress_sigaltstack()
 *	attempt to set up an alternative signal stack
 *	  stack - must be at least 4K
 *	  size  - size of stack (- STACK_ALIGNMENT)
 */
int stress_sigaltstack(const void *stack, const size_t size)
{
#if defined(__minix__)
	(void)stack;
	(void)size;
#else
	stack_t ss;

	if (size < (KB * 4)) {
		pr_err("sigaltstack stack size %zu must be more than 4K\n",
			size);
		return -1;
	}
	ss.ss_sp = align_address(stack, STACK_ALIGNMENT);
	ss.ss_size = size;
	ss.ss_flags = 0;
	if (sigaltstack(&ss, NULL) < 0) {
		pr_fail("sigaltstack failed: errno=%d (%s)\n",
			errno, strerror(errno));
		return -1;
	}
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
#if !defined(__gnu_hurd__) && !defined(__minix__)
	static bool set_altstack = false;

	/*
	 *  Signal handlers should really be using an alternative
	 *  signal stack to be totally safe.  For any new instance we
	 *  should set this alternative signal stack before setting
	 *  up any signal handler. We only need to do this once
	 *  per process instance, so just do it on the first
	 *  call to stress_sighandler.
	 */
	if (!set_altstack) {
		static uint8_t MLOCKED stack[SIGSTKSZ + STACK_ALIGNMENT];

		if (stress_sigaltstack(stack, SIGSTKSZ) < 0)
			return -1;
		set_altstack = true;
	}
#endif

	memset(&new_action, 0, sizeof new_action);
	new_action.sa_handler = handler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = SA_ONSTACK;

	if (sigaction(signum, &new_action, orig_action) < 0) {
		pr_fail("%s: sigaction %s: errno=%d (%s)\n",
			name, stress_strsignal(signum), errno, strerror(errno));
		return -1;
	}
	return 0;
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
#if defined(__linux__) && !defined(__PPC64__)
	int cpu;

	cpu = sched_getcpu();
	return (unsigned int)((cpu < 0) ? 0 : cpu);
#else
	return 0;
#endif
}

/*
 *  stress_not_implemented()
 *	report that a stressor is not implemented
 *	on a particular arch or kernel
 */
int stress_not_implemented(const args_t *args)
{
#if !defined(__NetBSD__) && !defined(__OpenBSD__) && !defined(__FreeBSD__)
	struct utsname buf;

	if (!uname(&buf)) {
		pr_inf("%s: this stressor is not implemented on this system: %s %s %s\n",
			args->name, buf.machine, buf.sysname, buf.release);
		return EXIT_NOT_IMPLEMENTED;
	}
#endif
	pr_inf("%s: this stressor is not implemented on this system\n",
		args->name);
	return EXIT_NOT_IMPLEMENTED;
}

#if defined(__linux__) && defined(F_SETPIPE_SZ)
/*
 *  stress_check_max_pipe_size()
 *	check if the given pipe size is allowed
 */
static inline size_t stress_check_max_pipe_size(
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
 *	determine the maximim allowed pipe size
 */
size_t stress_probe_max_pipe_size(void)
{
	static size_t max_pipe_size;

#if defined(__linux__) && defined(F_SETPIPE_SZ)
	size_t i, ret, prev_sz, sz, min, max;
	char buf[64];
	size_t page_size;
#endif
	/* Already determined? returned cached size */
	if (max_pipe_size)
		return max_pipe_size;

#if defined(__linux__) && defined(F_SETPIPE_SZ)
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
 *  align_address
 *	align address to alignment, alignment MUST be a power of 2
 */
void *align_address(const void *addr, const size_t alignment)
{
	const uintptr_t uintptr =
		((uintptr_t)addr + alignment) & ~(alignment - 1);

	return (void *)uintptr;
}

/*
 *  keep_stressing()
 *	returns true if we can keep on running a stressor
 */
bool HOT OPTIMIZE3 __keep_stressing(const args_t *args)
{
	return (LIKELY(g_keep_stressing_flag) &&
	        LIKELY(!args->max_ops || (*args->counter < args->max_ops)));
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
	} size_info_t;

	static const size_info_t size_info[] = {
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

	(void)snprintf(str, len, "%.1f%s", (double)val / scale, suffix);

	return str;
}
