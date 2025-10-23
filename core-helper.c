/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
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
#include "git-commit-id.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-cpu-cache.h"
#include "core-hash.h"
#include "core-numa.h"
#include "core-pragma.h"

#include <pwd.h>
#include <sys/ioctl.h>
#include <time.h>

#if defined(HAVE_SYS_LOADAVG_H)
#include <sys/loadavg.h>
#endif

#if defined(__FreeBSD__) &&	\
    defined(HAVE_SYS_PARAM_H)
#include <sys/param.h>
#endif

#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#endif

#if defined(HAVE_SYS_PROCCTL_H)
#include <sys/procctl.h>
#endif

#if defined(HAVE_SYS_SYSCTL_H) &&	\
    !defined(__linux__)
#include <sys/sysctl.h>
#endif

#if defined(HAVE_SYS_UTSNAME_H)
#include <sys/utsname.h>
#endif

/* prctl(2) timer slack support */
#if defined(HAVE_SYS_PRCTL_H) && \
    defined(HAVE_PRCTL) && \
    defined(PR_SET_TIMERSLACK) && \
    defined(PR_GET_TIMERSLACK)
#define HAVE_PRCTL_TIMER_SLACK
#endif

#if defined(HAVE_COMPILER_TCC) || defined(HAVE_COMPILER_PCC)
int __dso_handle;
#endif

#define PAGE_4K_SHIFT			(12)
#define PAGE_4K				(1 << PAGE_4K_SHIFT)

const char ALIGN64 NONSTRING stress_ascii64[64] =
	"0123456789ABCDEFGHIJKLMNOPQRSTUV"
	"WXYZabcdefghijklmnopqrstuvwxyz@!";

const char ALIGN64 NONSTRING stress_ascii32[32] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ_+@:#!";

/*
 *  stress_get_processors_online()
 *	get number of processors that are online
 */
int32_t stress_get_processors_online(void)
{
	static int32_t processors_online = 0;

	if (LIKELY(processors_online > 0))
		return processors_online;

#if defined(_SC_NPROCESSORS_ONLN)
	processors_online = (int32_t)sysconf(_SC_NPROCESSORS_ONLN);
	if (UNLIKELY(processors_online < 0))
		processors_online = 1;
#else
	processors_online = 1;
	UNEXPECTED
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

	if (LIKELY(processors_configured > 0))
		return processors_configured;

#if defined(_SC_NPROCESSORS_CONF)
	processors_configured = (int32_t)sysconf(_SC_NPROCESSORS_CONF);
	if (UNLIKELY(processors_configured < 0))
		processors_configured = stress_get_processors_online();
#else
	processors_configured = 1;
	UNEXPECTED
#endif
	return processors_configured;
}

/*
 *  stress_get_ticks_per_second()
 *	get number of ticks perf second
 */
int32_t stress_get_ticks_per_second(void)
{
#if defined(__fiwix)
	return 100;	/* Workaround */
#elif defined(_SC_CLK_TCK)
	static int32_t ticks_per_second = 0;

	if (LIKELY(ticks_per_second > 0))
		return ticks_per_second;

	ticks_per_second = (int32_t)sysconf(_SC_CLK_TCK);
	return ticks_per_second;
#else
	UNEXPECTED
	return -1;
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
#if defined(HAVE_GETLOADAVG) &&	\
    !defined(__UCLIBC__)
	int rc;
	double loadavg[3];

	if (UNLIKELY(!min1 || !min5 || !min15))
		return -1;

	loadavg[0] = 0.0;
	loadavg[1] = 0.0;
	loadavg[2] = 0.0;

	rc = getloadavg(loadavg, 3);
	if (UNLIKELY(rc < 0))
		goto fail;

	*min1 = loadavg[0];
	*min5 = loadavg[1];
	*min15 = loadavg[2];

	return 0;
fail:
#elif defined(HAVE_SYS_SYSINFO_H) &&	\
      defined(HAVE_SYSINFO) &&		\
      defined(SI_LOAD_SHIFT)
	struct sysinfo info;
	const double scale = 1.0 / (double)(1 << SI_LOAD_SHIFT);

	if (UNLIKELY(!min1 || !min5 || !min15))
		return -1;

	if (UNLIKELY(sysinfo(&info) < 0))
		goto fail;

	*min1 = info.loads[0] * scale;
	*min5 = info.loads[1] * scale;
	*min15 = info.loads[2] * scale;

	return 0;
fail:
#else
	if (UNLIKELY(!min1 || !min5 || !min15))
		return -1;
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
#elif defined(HAVE_SYS_PROCCTL_H) &&	\
      defined(__FreeBSD__) &&		\
      defined(PROC_PDEATHSIG_CTL)
	int sig = SIGALRM;

	(void)procctl(P_PID, 0, PROC_PDEATHSIG_CTL, &sig);
#else
	UNEXPECTED
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
	int rc = 0;

	(void)dumpable;

#if defined(RLIMIT_CORE)
	{
		struct rlimit lim;
		int ret;

		ret = getrlimit(RLIMIT_CORE, &lim);
		if (LIKELY(ret == 0)) {
			lim.rlim_cur = 0;
			(void)setrlimit(RLIMIT_CORE, &lim);
		}
		lim.rlim_cur = 0;
		lim.rlim_max = 0;
		(void)setrlimit(RLIMIT_CORE, &lim);
	}
#else
	UNEXPECTED
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

#if !defined(PR_SET_DISABLE)
#define SUID_DUMP_DISABLE	(0)       /* No setuid dumping */
#endif
#if !defined(SUID_DUMP_USER)
#define SUID_DUMP_USER		(1)       /* Dump as user of process */
#endif

	(void)prctl(PR_SET_DUMPABLE,
		dumpable ? SUID_DUMP_USER : SUID_DUMP_DISABLE);
#endif

#if defined(__linux__)
	{
		char const *str = dumpable ? "0x33" : "0x00";

		if (stress_system_write("/proc/self/coredump_filter", str, strlen(str)) < 0)
			rc = -1;
	}
#endif
	return rc;
}

/*
 *  stress_set_timer_slack_ns()
 *	set timer slack in nanoseconds
 */
int stress_set_timer_slack_ns(const char *opt)
{
#if defined(HAVE_PRCTL_TIMER_SLACK)
	uint32_t timer_slack;

	timer_slack = stress_get_uint32(opt);
	if (UNLIKELY(timer_slack == 0))
		pr_inf("note: setting timer_slack to 0 resets it to the default of 50,000 ns\n");
	(void)stress_set_setting("global", "timer-slack", TYPE_ID_UINT32, &timer_slack);
#else
	UNEXPECTED
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
#else
	UNEXPECTED
#endif
}

/*
 *  stress_set_proc_name_init()
 *	init setproctitle if supported
 */
void stress_set_proc_name_init(int argc, char *argv[], char *envp[])
{
#if defined(HAVE_SETPROCTITLE) && \
    defined(HAVE_SETPROCTITLE_INIT)
	(void)setproctitle_init(argc, argv, envp);
#else
	(void)argc;
	(void)argv;
	(void)envp;
	UNEXPECTED
#endif
}

/*
 *  stress_set_proc_name_raw()
 *	set process name as given, no special formatting
 */
void stress_set_proc_name_raw(const char *name)
{
	if (UNLIKELY(!name))
		return;
	if (g_opt_flags & OPT_FLAGS_KEEP_NAME)
		return;
#if defined(HAVE_SETPROCTITLE)
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
 *  stress_set_proc_name_scramble()
 *	turn pid and time now into a scrambled process name
 *	to fool any schedulers (e.g. sched_ext) that try to
 *	infer process scheduling policy from a process name
 *
 *	MUST NOT use mwc() functions as this maybe used in a
 *	signal context in the future and we need to avoid
 *	changing the mwc state.
 */
void stress_set_proc_name_scramble(void)
{
	char name[65];
	char *ptr;
	int i;
	uint32_t a, b, c, d;
	uint64_t rnd1, rnd2, rnd3, rnd4;
	double now;

	if (g_opt_flags & OPT_FLAGS_KEEP_NAME)
		return;

	now = stress_time_now();

	rnd1 = (uint64_t)getpid();
	rnd2 = (uint64_t)((double)rnd1 * now);
	rnd2 = shim_ror64n(rnd1, rnd1 & 0x63);

	/* generate scrambled bit patterns via hashing */
	a = stress_hash_murmur3_32((uint8_t *)&now, sizeof(now), (uint32_t)rnd2);
	b = stress_hash_mulxror64((char *)&rnd1, sizeof(rnd2)) ^ ~rnd1;
	c = stress_hash_coffin32_be((char *)&now, sizeof(now)) ^ stress_get_cpu();
	d = stress_hash_coffin32_le((char *)&rnd1, sizeof(rnd1));

	rnd1 = ((uint64_t)a << 32) | (uint64_t)b;
	rnd2 = ((uint64_t)c << 32) | (uint64_t)d;
	rnd3 = ((uint64_t)a << 32) | (uint64_t)c;
	rnd4 = ((uint64_t)b << 32) | (uint64_t)d;

	/* scramble part 1 */
	for (i = 0; i < (int)(a & 0x3); i++) {
		rnd1 = shim_rol64n(rnd1, 3) ^ rnd3;
		rnd2 = shim_ror64n(rnd2, 1) ^ rnd4;
		rnd3 = shim_rol64n(rnd3, 7);
		rnd4 = shim_ror64n(rnd3, 11);
	}

	/* generate 64 char name */
	for (ptr = name, i = 0; i < 16; i++) {
		rnd1 = shim_rol64n(rnd1, 3) ^ rnd3;
		rnd2 = shim_ror64n(rnd2, 1) ^ rnd4;
		rnd3 = shim_rol64n(rnd3, 7);
		rnd4 = shim_ror64n(rnd3, 11);

		*ptr++ = stress_ascii64[rnd1 & 0x3f];
		*ptr++ = stress_ascii64[rnd2 & 0x3f];
		*ptr++ = stress_ascii32[rnd3 & 0x1f];	/* intentional */
		*ptr++ = stress_ascii64[rnd4 & 0x3f];
	}

	/* and punch in some unusual charset chars */
	for (i = 1; i < 64;) {
		const uint8_t v = (rnd1 & 1) | (rnd2 & 2) | (rnd3 & 4) | (rnd4 & 8);

		switch (v) {
		case 3:
			name[i] = '-';
			break;
		case 11:
			name[i] = ' ';
			break;
		case 13:
			name[i] = '.';
			break;
		case 15:
			name[i] = '/';
		}
		rnd1 = shim_rol64(rnd1);
		rnd2 = shim_rol64(rnd2);
		rnd3 = shim_rol64(rnd3);
		rnd4 = shim_rol64(rnd4);

		i += v & 3;
	}
	*ptr = '\0';
	stress_set_proc_name_raw(name);
}

/*
 *  stress_set_proc_name()
 *	Set process name, we don't care if it fails
 */
void stress_set_proc_name(const char *name)
{
	char long_name[64];

	if (g_opt_flags & OPT_FLAGS_RANDPROCNAME) {
		stress_set_proc_name_scramble();
		return;
	}
	if (UNLIKELY(!name))
		return;
	(void)snprintf(long_name, sizeof(long_name), "%s-%s",
			g_app_name, name);
	stress_set_proc_name_raw(long_name);
}

/*
 *  stress_set_proc_state_str
 *	set process name based on run state string, see
 *	macros STRESS_STATE_*
 */
void stress_set_proc_state_str(const char *name, const char *str)
{
	char long_name[64];

	(void)str;
	if (g_opt_flags & OPT_FLAGS_RANDPROCNAME) {
		stress_set_proc_name_scramble();
		return;
	}
	if (UNLIKELY(!name))
		return;
	(void)snprintf(long_name, sizeof(long_name), "%s-%s",
			g_app_name, name);
	stress_set_proc_name_raw(long_name);
}

/*
 *  stress_set_proc_state
 *	set process name based on run state, see
 *	macros STRESS_STATE_*
 */
void stress_set_proc_state(const char *name, const int state)
{
	static const char * const stress_states[] = {
		"start",
		"init",
		"run",
		"syncwait",
		"deinit",
		"stop",
		"exit",
		"wait",
		"zombie",
	};

	if (UNLIKELY(!name))
		return;
	if (UNLIKELY((state < 0) || (state >= (int)SIZEOF_ARRAY(stress_states))))
		return;

	stress_set_proc_state_str(name, stress_states[state]);
}

/*
 *  stress_chr_munge()
 *	convert ch _ to -, otherwise don't change it
 */
static inline char CONST stress_chr_munge(const char ch)
{
	return (ch == '_') ? '-' : ch;
}

/*
 *   stress_munge_underscore()
 *	turn '_' to '-' in strings with strscpy api
 */
size_t stress_munge_underscore(char *dst, const char *src, size_t len)
{
	register char *d = dst;
	register const char *s = src;
	register size_t n = len;

	if (LIKELY(n)) {
		while (--n) {
			register char c = *s++;

			*d++ = stress_chr_munge(c);
			if (c == '\0')
				break;
		}
	}

	if (!n) {
		if (len)
			*d = '\0';
		while (*s)
			s++;
	}

	return (s - src - 1);
}

/*
 *  stress_strcmp_munged()
 *	compare strings with _ comcompared to -
 */
int stress_strcmp_munged(const char *s1, const char *s2)
{
	for (; *s1 && (stress_chr_munge(*s1) == stress_chr_munge(*s2)); s1++, s2++)
		;

	return (unsigned char)stress_chr_munge(*s1) - (unsigned char)stress_chr_munge(*s2);
}

/*
 *  stress_get_uint64_zero()
 *	return uint64 zero in way that force less smart
 *	static analysers to realise we are doing this
 *	to force a division by zero. I'd like to have
 *	a better solution than this ghastly way.
 */
uint64_t stress_get_uint64_zero(void)
{
	return g_shared->zero;
}

/*
 *  stress_get_uint64_zero()
 *	return null in way that force less smart
 *	static analysers to realise we are doing this
 *	to force a division by zero. I'd like to have
 *	a better solution than this ghastly way.
 */
void *stress_get_null(void)
{
	return (void *)(uintptr_t)g_shared->zero;
}

/*
 *  stress_little_endian()
 *	returns true if CPU is little endian
 */
bool CONST stress_little_endian(void)
{
	const uint32_t x = 0x12345678;
	const uint8_t *y = (const uint8_t *)&x;

	return *y == 0x78;
}

/*
 *  stress_endian_str()
 *	return endianness as a string
 */
static const char * CONST stress_endian_str(void)
{
	return stress_little_endian() ? "little endian" : "big endian";
}

/*
 *  stress_get_libc_version()
 *	return human readable libc version (where possible)
 */
static char *stress_get_libc_version(void)
{
#if defined(__GLIBC__) &&	\
    defined(__GLIBC_MINOR__)
	static char buf[64];

	(void)snprintf(buf, sizeof(buf), "glibc %d.%d", __GLIBC__, __GLIBC_MINOR__);
	return buf;
#elif defined(__UCLIBC__) &&		\
    defined(__UCLIBC_MAJOR__) &&	\
    defined(__UCLIBC_MINOR__)
	static char buf[64];

	(void)snprintf(buf, sizeof(buf), "uclibc %d.%d", __UCLIBC_MAJOR__, __UCLIBC_MINOR__);
	return buf;
#elif defined(__CYGWIN__)
	return "Cygwin libc";
#elif defined(__DARWIN_C_LEVEL)
	return "Darwin libc";
#elif defined(HAVE_CC_MUSL_GCC)
	/* Built with MUSL_GCC, highly probably it's musl libc being used too */
	return "musl libc";
#elif defined(__HAIKU__)
	return "Haiku libc";
#else
	return "unknown libc version";
#endif
}

#define XSTR(s) STR(s)
#define STR(s) #s

/*
 *  stress_buildinfo()
 *     info about compiler, built date and compilation flags
 */
void stress_buildinfo(void)
{
	if (g_opt_flags & OPT_FLAGS_BUILDINFO) {
		pr_inf("compiler: %s\n", stress_get_compiler());
#if defined(HAVE_SOURCE_DATE_EPOCH)
		pr_inf("SOURCE_DATE_EPOCH: " XSTR(HAVE_SOURCE_DATE_EPOCH) "\n");
#endif
#if defined(HAVE_EXTRA_BUILDINFO)
#if defined(HAVE_CFLAGS)
		pr_inf("CFLAGS: " HAVE_CFLAGS "\n");
#endif
#if defined(HAVE_CXXFLAGS)
		pr_inf("CXXFLAGS: " HAVE_CXXFLAGS "\n");
#endif
#if defined(HAVE_LDFLAGS)
		pr_inf("LDFLAGS: " HAVE_LDFLAGS "\n");
#endif
#endif
#if defined(__STDC_VERSION__)
		pr_inf("STDC Version: " XSTR(__STDC_VERSION__) "\n");
#endif
#if defined(__STDC_HOSTED__)
		pr_inf("STDC Hosted: " XSTR(__STDC_HOSTED__) "\n");
#endif
#if defined(BUILD_STATIC)
		pr_inf("Build: static image\n");
#else
		pr_inf("Build: dynamic link\n");
#endif
	}
}

/*
 *  stress_yaml_buildinfo()
 *     log info about compiler, built date and compilation flags
 */
void stress_yaml_buildinfo(FILE *yaml)
{
	if (UNLIKELY(!yaml))
		return;

	pr_yaml(yaml, "build-info:\n");
	pr_yaml(yaml, "      compiler: '%s'\n", stress_get_compiler());
#if defined(HAVE_SOURCE_DATE_EPOCH)
	pr_yaml(yaml, "      source-date-epoch: " XSTR(HAVE_SOURCE_DATE_EPOCH) "\n");
#endif
#if defined(HAVE_EXTRA_BUILDINFO)
#if defined(HAVE_CFLAGS)
	pr_yaml(yaml, "      cflags: '" HAVE_CFLAGS "'\n");
#endif
#if defined(HAVE_CXXFLAGS)
	pr_yaml(yaml, "      cxxflags: '" HAVE_CXXFLAGS "'\n");
#endif
#if defined(HAVE_LDFLAGS)
	pr_yaml(yaml, "      ldflags: '" HAVE_LDFLAGS "'\n");
#endif
#endif
#if defined(__STDC_VERSION__)
	pr_yaml(yaml, "      stdc-version: '" XSTR(__STDC_VERSION__) "'\n");
#endif
#if defined(__STDC_HOSTED__)
	pr_yaml(yaml, "      stdc-hosted: '" XSTR(__STDC_HOSTED__) "'\n");
#endif
	pr_yaml(yaml, "\n");
}


#undef XSTR
#undef STR

/*
 *  stress_runinfo()
 *	short info about the system we are running stress-ng on
 *	for the -v option
 */
void stress_runinfo(void)
{
	char real_path[PATH_MAX], *real_path_ret;
	const char *temp_path = stress_get_temp_path();
	const char *fs_type = stress_get_fs_type(temp_path);
	size_t freemem, totalmem, freeswap, totalswap;
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	struct utsname uts;
#endif
	if (!(g_opt_flags & OPT_FLAGS_PR_DEBUG))
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
	if (LIKELY(uname(&uts) >= 0)) {
		pr_dbg("system: %s %s %s %s %s, %s, %s, %s\n",
			uts.sysname, uts.nodename, uts.release,
			uts.version, uts.machine,
			stress_get_compiler(),
			stress_get_libc_version(),
			stress_endian_str());
	}
#else
	pr_dbg("system: %s, %s, %s, %s\n",
		stress_get_arch(),
		stress_get_compiler(),
		stress_get_libc_version(),
		stress_endian_str());
#endif
	if (stress_get_meminfo(&freemem, &totalmem, &freeswap, &totalswap) == 0) {
		char ram_t[32], ram_f[32], ram_s[32];

		stress_uint64_to_str(ram_t, sizeof(ram_t), (uint64_t)totalmem, 1, false);
		stress_uint64_to_str(ram_f, sizeof(ram_f), (uint64_t)freemem, 1, false);
		stress_uint64_to_str(ram_s, sizeof(ram_s), (uint64_t)freeswap, 1, false);
		pr_dbg("RAM total: %s, RAM free: %s, swap free: %s\n", ram_t, ram_f, ram_s);
	}
	real_path_ret = realpath(temp_path, real_path);
	pr_dbg("temporary file path: '%s'%s\n", real_path_ret ? real_path : temp_path, fs_type);
}

/*
 *  stress_yaml_runinfo()
 *	log info about the system we are running stress-ng on
 */
void stress_yaml_runinfo(FILE *yaml)
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
	const size_t hostname_len = stress_get_hostname_length();
	char *hostname;
	const char *user = shim_getlogin();

	if (UNLIKELY(!yaml))
		return;

	pr_yaml(yaml, "system-info:\n");
	if (time(&t) != ((time_t)-1))
		tm = localtime(&t);

	pr_yaml(yaml, "      stress-ng-version: '" VERSION "'\n");
	pr_yaml(yaml, "      run-by: '%s'\n", user ? user : "unknown");
	if (LIKELY(tm != NULL)) {
		pr_yaml(yaml, "      date-yyyy-mm-dd: '%4.4d:%2.2d:%2.2d'\n",
			tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
		pr_yaml(yaml, "      time-hh-mm-ss: '%2.2d:%2.2d:%2.2d'\n",
			tm->tm_hour, tm->tm_min, tm->tm_sec);
		pr_yaml(yaml, "      epoch-secs: %ld\n", (long int)t);
	}

	hostname = (char *)malloc(hostname_len + 1);
	if (hostname && !gethostname(hostname, hostname_len)) {
		pr_yaml(yaml, "      hostname: '%s'\n", hostname);
	} else {
		pr_yaml(yaml, "      hostname: '%s'\n", "unknown");
	}
	free(hostname);

#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	if (LIKELY(uname(&uts) >= 0)) {
		pr_yaml(yaml, "      sysname: '%s'\n", uts.sysname);
		pr_yaml(yaml, "      nodename: '%s'\n", uts.nodename);
		pr_yaml(yaml, "      release: '%s'\n", uts.release);
		pr_yaml(yaml, "      version: '%s'\n", uts.version);
		pr_yaml(yaml, "      machine: '%s'\n", uts.machine);
	}
#else
	pr_yaml(yaml, "      machine: '%s'\n", stress_get_arch());
#endif
	pr_yaml(yaml, "      compiler: '%s'\n", stress_get_compiler());
	pr_yaml(yaml, "      libc: '%s'\n", stress_get_libc_version());
#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO)
	(void)shim_memset(&info, 0, sizeof(info));
	if (LIKELY(sysinfo(&info) == 0)) {
		pr_yaml(yaml, "      uptime: %ld\n", (long int)info.uptime);
		pr_yaml(yaml, "      totalram: %lu\n", (long unsigned int)info.totalram);
		pr_yaml(yaml, "      freeram: %lu\n", (long unsigned int)info.freeram);
		pr_yaml(yaml, "      sharedram: %lu\n", (long unsigned int)info.sharedram);
		pr_yaml(yaml, "      bufferram: %lu\n", (long unsigned int)info.bufferram);
		pr_yaml(yaml, "      totalswap: %lu\n", (long unsigned int)info.totalswap);
		pr_yaml(yaml, "      freeswap: %lu\n", (long unsigned int)info.freeswap);
	}
#endif
	pr_yaml(yaml, "      pagesize: %zd\n", stress_get_page_size());
	pr_yaml(yaml, "      cpus: %" PRId32 "\n", stress_get_processors_configured());
	pr_yaml(yaml, "      cpus-online: %" PRId32 "\n", stress_get_processors_online());
	pr_yaml(yaml, "      ticks-per-second: %" PRId32 "\n", stress_get_ticks_per_second());
	pr_yaml(yaml, "\n");
}

/*
 *  stress_get_cpu()
 *	get cpu number that process is currently on
 */
unsigned int stress_get_cpu(void)
{
#if defined(HAVE_SCHED_GETCPU)
#if defined(__PPC64__) || defined(__ppc64__) ||	\
    defined(__PPC__) || defined(__ppc__) ||	\
    defined(__s390x__)
	unsigned int cpu, node;

	if (UNLIKELY(shim_getcpu(&cpu, &node, NULL) < 0))
		return 0;
	return cpu;
#else
	const int cpu = sched_getcpu();

	return (unsigned int)((cpu < 0) ? 0 : cpu);
#endif
#else
	unsigned int cpu, node;

	if (UNLIKELY(shim_getcpu(&cpu, &node, NULL) < 0))
		return 0;
	return cpu;
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
#if   defined(HAVE_COMPILER_ICC) &&	\
      defined(__INTEL_COMPILER) &&	\
      defined(__INTEL_COMPILER_UPDATE) && \
      defined(__INTEL_COMPILER_BUILD_DATE)
	static const char cc[] = "icc " XSTRINGIFY(__INTEL_COMPILER) "." XSTRINGIFY(__INTEL_COMPILER_UPDATE) " Build " XSTRINGIFY(__INTEL_COMPILER_BUILD_DATE) "";
#elif defined(HAVE_COMPILER_ICC) && 		\
      defined(__INTEL_COMPILER) &&	\
      defined(__INTEL_COMPILER_UPDATE)
	static const char cc[] = "icc " XSTRINGIFY(__INTEL_COMPILER) "." XSTRINGIFY(__INTEL_COMPILER_UPDATE) "";
#elif defined(__INTEL_CLANG_COMPILER)
	static const char cc[] = "icx " XSTRINGIFY(__INTEL_CLANG_COMPILER) "";
#elif defined(__INTEL_LLVM_COMPILER)
	static const char cc[] = "icx " XSTRINGIFY(__INTEL_LLVM_COMPILER) "";
#elif defined(__TINYC__)
	static const char cc[] = "tcc " XSTRINGIFY(__TINYC__) "";
#elif defined(__PCC__) &&			\
       defined(__PCC_MINOR__)
	static const char cc[] = "pcc " XSTRINGIFY(__PCC__) "." XSTRINGIFY(__PCC_MINOR__) "." XSTRINGIFY(__PCC_MINORMINOR__) "";
#elif defined(__clang_major__) &&	\
      defined(__clang_minor__) &&	\
      defined(__clang_patchlevel__)
	static const char cc[] = "clang " XSTRINGIFY(__clang_major__) "." XSTRINGIFY(__clang_minor__) "." XSTRINGIFY(__clang_patchlevel__) "";
#elif defined(__clang_major__) &&	\
      defined(__clang_minor__)
	static const char cc[] = "clang " XSTRINGIFY(__clang_major__) "." XSTRINGIFY(__clang_minor__) "";
#elif defined(__GNUC__) &&		\
      defined(__GNUC_MINOR__) &&	\
      defined(__GNUC_PATCHLEVEL__) &&	\
      defined(HAVE_COMPILER_MUSL)
	static const char cc[] = "musl-gcc " XSTRINGIFY(__GNUC__) "." XSTRINGIFY(__GNUC_MINOR__) "." XSTRINGIFY(__GNUC_PATCHLEVEL__) "";
#elif defined(__GNUC__) &&		\
      defined(__GNUC_MINOR__) &&	\
      defined(HAVE_COMPILER_MUSL)
	static const char cc[] = "musl-gcc " XSTRINGIFY(__GNUC__) "." XSTRINGIFY(__GNUC_MINOR__) "";
#elif defined(__GNUC__) &&		\
      defined(__GNUC_MINOR__) &&	\
      defined(__GNUC_PATCHLEVEL__) &&	\
      defined(HAVE_COMPILER_GCC)
	static const char cc[] = "gcc " XSTRINGIFY(__GNUC__) "." XSTRINGIFY(__GNUC_MINOR__) "." XSTRINGIFY(__GNUC_PATCHLEVEL__) "";
#elif defined(__GNUC__) &&		\
      defined(__GNUC_MINOR__) &&	\
      defined(HAVE_COMPILER_GCC)
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

	if (LIKELY(uname(&buf) >= 0)) {
		static char str[sizeof(buf.machine) +
	                        sizeof(buf.sysname) +
				sizeof(buf.release) + 3];

		(void)snprintf(str, sizeof(str), "%s %s %s", buf.machine, buf.sysname, buf.release);
		return str;
	}
#else
	UNEXPECTED
#endif
	return "unknown";
}

/*
 *  stress_unimplemented()
 *	report that a stressor is not implemented
 *	on a particular arch or kernel
 */
int CONST stress_unimplemented(stress_args_t *args)
{
	(void)args;

	return EXIT_NOT_IMPLEMENTED;
}

/*
 *  stress_uint64_to_str()
 *	turn 64 bit size to human readable string, if no_zero is true, truncate
 *	to integer if decimal part is zero
 */
char *stress_uint64_to_str(char *str, size_t len, const uint64_t val, const int precision, bool no_zero)
{
	typedef struct {
		const uint64_t size;
		const char *suffix;
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
	const char *suffix = "";
	uint64_t scale = 1;
	int prec = precision;

	if (UNLIKELY((!str) || (len < 1)))
		return str;

	for (i = 0; i < SIZEOF_ARRAY(size_info); i++) {
		const uint64_t scaled = val / size_info[i].size;

		if ((scaled >= 1) && (scaled < 1024)) {
			suffix = size_info[i].suffix;
			scale = size_info[i].size;
			break;
		}
	}

	if (no_zero && ((val % scale) == 0))
		prec = 0;
	(void)snprintf(str, len, "%.*f%s", prec, (double)val / (double)scale, suffix);

	return str;
}

/*
 *  stress_const_optdup(const char *opt)
 *	duplicate a modifiable copy of a const option string opt
 */
char *stress_const_optdup(const char *opt)
{
	char *str;

	if (UNLIKELY(!opt))
		return NULL;

	str = shim_strdup(opt);
	if (UNLIKELY(!str))
		(void)fprintf(stderr, "out of memory duplicating option '%s'\n", opt);

	return str;
}

/*
 *  stress_get_exec_text_addr()
 *	return length and start/end addresses of text segment
 */
size_t stress_exec_text_addr(char **start, char **end)
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
#elif defined(HAVE_COMPILER_TCC)
	extern char _start;
	intptr_t text_start = (intptr_t)&_start;
#elif defined(__CYGWIN__)
	extern char WinMainCRTStartup;
	intptr_t text_start = (intptr_t)&WinMainCRTStartup;
#else
	extern char _start;
	intptr_t text_start = (intptr_t)&_start;
#endif

#if defined(__APPLE__)
	extern void *get_etext(void);
	intptr_t text_end = (intptr_t)get_etext();
#elif defined(HAVE_COMPILER_TCC)
	extern char _etext;
	intptr_t text_end = (intptr_t)&_etext;
#else
	extern char etext;
	intptr_t text_end = (intptr_t)&etext;
#endif
	if (UNLIKELY(text_end <= text_start))
		return 0;

	if (UNLIKELY((start == NULL) || (end == NULL) || (text_start >= text_end)))
		return 0;

	*start = (char *)text_start;
	*end = (char *)text_end;
	return (size_t)(text_end - text_start);
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

	if (UNLIKELY(!name))
		return true;
	return !strncmp("/dev/tty", name, 8);
#else
	UNEXPECTED
	(void)fd;

	/* Assume it is */
	return true;
#endif
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

	if (UNLIKELY(!g_shared))
		return true;

	if (stress_lock_acquire(g_shared->warn_once.lock) < 0)
		return true;
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
	stress_lock_release(g_shared->warn_once.lock);

	return not_warned_yet;
}

/*
 *  stress_uid_comp()
 *	uid comparison for sorting
 */
#if defined(HAVE_SETPWENT) &&	\
    defined(HAVE_GETPWENT) &&	\
    defined(HAVE_ENDPWENT) &&	\
    !defined(BUILD_STATIC)
static CONST int stress_uid_comp(const void *p1, const void *p2)
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

	if (!uid)
		return -1;
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

		uids = (uid_t *)calloc(n, sizeof(*uids));
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
	if (uid)
		*uid = 0;

	return -1;
}
#endif

/*
 *  stress_kernel_release()
 *	turn release major.minor.patchlevel triplet into base 100 value
 */
int CONST stress_kernel_release(const int major, const int minor, const int patchlevel)
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
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	struct utsname buf;
	int major = 0, minor = 0, patchlevel = 0;

	if (UNLIKELY(uname(&buf) < 0))
		return -1;

	if (sscanf(buf.release, "%d.%d.%d\n", &major, &minor, &patchlevel) < 1)
		return -1;

	return stress_kernel_release(major, minor, patchlevel);
#else
	UNEXPECTED
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
#if defined(PID_MAX_LIMIT)
	pid_t max_pid = STRESS_MAXIMUM(PID_MAX_LIMIT, 1024);
#elif defined(PID_MAX)
	pid_t max_pid = STRESS_MAXIMUM(PID_MAX, 1024);
#elif defined(PID_MAX_DEFAULT)
	pid_t max_pid = STRESS_MAXIMUM(PID_MAX_DEFAULT, 1024);
#else
	pid_t max_pid = 32767;
#endif
	int i;
	pid_t pid;
	uint32_t n;
	char buf[64];

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
			int status;
			pid_t ret;

			ret = waitpid(pid, &status, 0);
			if ((ret == pid) &&
			    ((shim_kill(pid, 0) < 0) && (errno == ESRCH))) {
				return pid;
			}
		}
	}

	/*
	 *  Make a random PID guess.
	 */
	n = (uint32_t)max_pid - 1023;
	for (i = 0; i < 10; i++) {
		pid = (pid_t)stress_mwc32modn(n) + 1023;

		if ((shim_kill(pid, 0) < 0) && (errno == ESRCH))
			return pid;
	}

	(void)shim_memset(buf, 0, sizeof(buf));
	if (stress_system_read("/proc/sys/kernel/pid_max", buf, sizeof(buf) - 1) > 0) {
		long val;

		if (sscanf(buf, "%ld", &val) == 1)
			max_pid = (pid_t)val;
	}

	n = (uint32_t)max_pid - 1023;
	for (i = 0; i < 10; i++) {
		pid = (pid_t)stress_mwc32modn(n) + 1023;

		if ((shim_kill(pid, 0) < 0) && (errno == ESRCH))
			return pid;
	}

	/*
	 *  Give up.
	 */
	return max_pid;
}

/*
 *  stress_get_hostname_length()
 *	return the maximum allowed hostname length
 */
size_t stress_get_hostname_length(void)
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
 *  stress_get_tty_width()
 *	get tty column width
 */
int stress_get_tty_width(void)
{
	const int default_width = 80;
#if defined(HAVE_WINSIZE) &&	\
    defined(TIOCGWINSZ)
	struct winsize ws;
	int ret, fd;

	if (stress_is_a_pipe(fileno(stdout)))
		fd = fileno(stdin);
	else
		fd = fileno(stdout);

	ret = ioctl(fd, TIOCGWINSZ, &ws);
	if (UNLIKELY(ret < 0))
		return default_width;
	ret = (int)ws.ws_col;
	if (UNLIKELY((ret <= 0) || (ret > 1024)))
		return default_width;
	return ret;
#else
	return default_width;
#endif
}

/*
 *  stress_redo_fork()
 *	check fork errno (in err) and return true if
 *	an immediate fork can be retried due to known
 *	error cases that are retryable. Also force a
 *	scheduling yield.
 */
bool stress_redo_fork(stress_args_t *args, const int err)
{
	/* Timed out! */
	if (UNLIKELY(stress_time_now() > args->time_end)) {
		stress_continue_set_flag(false);
		return false;
	}
	/* More bogo-ops to go and errors indicate a fork retry? */
	if (LIKELY(stress_continue(args)) &&
	    ((err == EAGAIN) || (err == EINTR) || (err == ENOMEM))) {
		(void)shim_sched_yield();
		return true;
	}
	return false;
}

/*
 *  stress_clear_warn_once()
 *	clear the linux warn once warnings flag, kernel warn once
 *	messages can be re-issued
 */
void stress_clear_warn_once(void)
{
#if defined(__linux__)
	if (stress_check_capability(SHIM_CAP_IS_ROOT))
		(void)stress_system_write("/sys/kernel/debug/clear_warn_once", "1", 1);
#endif
}

/*
 *  stress_flag_permutation()
 *	given flag mask in flags, generate all possible permutations
 *	of bit flags. e.g.
 *		flags = 0x81;
 *			-> b00000000
 *			   b00000001
 *			   b10000000
 *			   b10000001
 */
size_t stress_flag_permutation(const int flags, int **permutations)
{
	unsigned int flag_bits;
	unsigned int n_bits;
	register unsigned int j, n_flags;
	int *perms;

	if (UNLIKELY(!permutations))
		return 0;

	*permutations = NULL;

	for (n_bits = 0, flag_bits = (unsigned int)flags; flag_bits; flag_bits >>= 1U)
		n_bits += (flag_bits & 1U);

	if (n_bits > STRESS_MAX_PERMUTATIONS)
		n_bits = STRESS_MAX_PERMUTATIONS;

	n_flags = 1U << n_bits;
	perms = (int *)calloc((size_t)n_flags, sizeof(*perms));
	if (UNLIKELY(!perms))
		return 0;

	/*
	 *  Generate all the possible flag settings in order
	 */
	for (j = 0; j < n_flags; j++) {
		register int i;
		register unsigned int j_mask = 1U;

		for (i = 0; i < 32; i++) {
			const int i_mask = (int)(1U << i);

			if (flags & i_mask) {
				if (j & j_mask)
					perms[j] |= i_mask;
				j_mask <<= 1U;
			}
		}
	}
	*permutations = perms;
	return (size_t)n_flags;
}

/*
 *  Indicate a stress test failed because of limited resources
 *  rather than a failure of the tests during execution.
 *  err is the errno of the failure.
 */
int CONST stress_exit_status(const int err)
{
	switch (err) {
	case ENOMEM:
	case ENOSPC:
		return EXIT_NO_RESOURCE;
	case ENOSYS:
		return EXIT_NOT_IMPLEMENTED;
	}
	return EXIT_FAILURE;	/* cppcheck-suppress ConfigurationNotChecked */
}

/*
 *  stress_get_proc_self_exe_path()
 *	get process' executable path via readlink
 */
static char *stress_get_proc_self_exe_path(char *path, const char *proc_path, const size_t path_len)
{
	ssize_t len;

	if (UNLIKELY(!path || !proc_path))
		return NULL;

	len = shim_readlink(proc_path, path, path_len);
	if (UNLIKELY((len < 0) || (len >= PATH_MAX)))
		return NULL;
	path[len] = '\0';

	return path;
}

/*
 *  stress_get_proc_self_exe()
 *  	determine the path to the executable, return NULL if not possible/failed
 */
char *stress_get_proc_self_exe(char *path, const size_t path_len)
{
#if defined(__linux__)
	return stress_get_proc_self_exe_path(path, "/proc/self/exe", path_len);
#elif defined(__NetBSD__)
	return stress_get_proc_self_exe_path(path, "/proc/curproc/exe", path_len);
#elif defined(__DragonFly__)
	return stress_get_proc_self_exe_path(path, "/proc/curproc/file", path_len);
#elif defined(__FreeBSD__)
#if defined(CTL_KERN) &&	\
    defined(KERN_PROC) &&	\
    defined(KERN_PROC_PATHNAME)
	static int mib[] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
	size_t tmp_path_len = path_len;
	int ret;

	if (UNLIKELY(!path))
		return NULL;

	ret = sysctl(mib, SIZEOF_ARRAY(mib), (void *)path, &tmp_path_len, NULL, 0);
	if (ret < 0) {
		/* fall back to procfs */
		return stress_get_proc_self_exe_path(path, "/proc/curproc/file", path_len);
	}
	return path;
#else
	/* fall back to procfs */
	if (UNLIKELY(!path))
		return NULL;
	return stress_get_proc_self_exe_path(path, "/proc/curproc/file", path_len);
#endif
#elif defined(__sun__) && 	\
      defined(HAVE_GETEXECNAME)
	const char *execname = getexecname();

	if (UNLIKELY(!path))
		return NULL;
	(void)stress_get_proc_self_exe_path;

	if (UNLIKELY(!execname))
		return NULL;
	/* Need to perform a string copy to deconstify execname */
	(void)shim_strscpy(path, execname, path_len);
	return path;
#elif defined(HAVE_PROGRAM_INVOCATION_NAME)
	if (UNLIKELY(!path))
		return NULL;

	(void)stress_get_proc_self_exe_path;

	/* this may return the wrong name if it's been argv modified */
	(void)shim_strscpy(path, program_invocation_name, path_len);
	return path;
#else
	if (UNLIKELY(!path))
		return NULL;
	(void)stress_get_proc_self_exe_path;
	(void)path;
	(void)path_len;
	return NULL;
#endif
}

#if defined(__FreeBSD__) ||	\
    defined(__NetBSD__) ||	\
    defined(__APPLE__)
/*
 *  stress_bsd_getsysctl()
 *	get sysctl using name, ptr to obj, size = size of obj
 */
int stress_bsd_getsysctl(const char *name, void *ptr, size_t size)
{
	int ret;
	size_t nsize = size;

	if (UNLIKELY(!ptr || !name))
		return -1;

	(void)shim_memset(ptr, 0, size);

	ret = sysctlbyname(name, ptr, &nsize, NULL, 0);
	if ((ret < 0) || (nsize != size)) {
		(void)shim_memset(ptr, 0, size);
		return -1;
	}
	return 0;
}

/*
 *  stress_bsd_getsysctl_uint64()
 *	get sysctl by name, return uint64 value
 */
uint64_t stress_bsd_getsysctl_uint64(const char *name)
{
	uint64_t val;

	if (UNLIKELY(!name))
		return 0ULL;
	if (stress_bsd_getsysctl(name, &val, sizeof(val)) == 0)
		return val;
	return 0ULL;
}

/*
 *  stress_bsd_getsysctl_uint32()
 *	get sysctl by name, return uint32 value
 */
uint32_t stress_bsd_getsysctl_uint32(const char *name)
{
	uint32_t val;

	if (UNLIKELY(!name))
		return 0UL;
	if (stress_bsd_getsysctl(name, &val, sizeof(val)) == 0)
		return val;
	return 0UL;
}

/*
 *  stress_bsd_getsysctl_uint()
 *	get sysctl by name, return unsigned int value
 */
unsigned int stress_bsd_getsysctl_uint(const char *name)
{
	unsigned int val;

	if (UNLIKELY(!name))
		return 0;
	if (stress_bsd_getsysctl(name, &val, sizeof(val)) == 0)
		return val;
	return 0;
}

/*
 *  stress_bsd_getsysctl_int()
 *	get sysctl by name, return int value
 */
int stress_bsd_getsysctl_int(const char *name)
{
	int val;

	if (UNLIKELY(!name))
		return 0;
	if (stress_bsd_getsysctl(name, &val, sizeof(val)) == 0)
		return val;
	return 0;
}
#else

int CONST stress_bsd_getsysctl(const char *name, void *ptr, size_t size)
{
	(void)name;
	(void)ptr;
	(void)size;

	return 0;
}

uint64_t CONST stress_bsd_getsysctl_uint64(const char *name)
{
	(void)name;

	return 0ULL;
}

uint32_t CONST stress_bsd_getsysctl_uint32(const char *name)
{
	(void)name;

	return 0UL;
}

unsigned int CONST stress_bsd_getsysctl_uint(const char *name)
{
	(void)name;

	return 0;
}

int CONST stress_bsd_getsysctl_int(const char *name)
{
	(void)name;

	return 0;
}
#endif

/*
 *  stress_x86_readmsr()
 *	64 bit read an MSR on a specified x86 CPU
 */
int stress_x86_readmsr64(const int cpu, const uint32_t reg, uint64_t *val)
{
#if defined(STRESS_ARCH_X86)
	char buffer[PATH_MAX];
	uint64_t value = 0;
	int fd;
	ssize_t ret;

	if (UNLIKELY(!val))
		return -1;
	*val = ~0ULL;
	(void)snprintf(buffer, sizeof(buffer), "/dev/cpu/%d/msr", cpu);
	if ((fd = open(buffer, O_RDONLY)) < 0)
		return -1;

	ret = pread(fd, &value, 8, reg);
	(void)close(fd);
	if (ret < 0)
		return -1;

	*val = value;
	return 0;
#else
	(void)cpu;
	(void)reg;
	(void)val;

	if (val)
		*val = ~0ULL;
	return -1;
#endif
}

/*
 *  stress_random_small_sleep()
 *	0..5000 us sleep, used in pthreads to add some
 *	small delay into startup to randomize any racy
 *	conditions
 */
void stress_random_small_sleep(void)
{
	shim_usleep_interruptible(stress_mwc32modn(5000));
}

/*
 *  stress_yield_sleep_ms()
 *	force a yield, sleep if the yield was less than 1ms,
 *      and repeat if sleep was less than 1ms
 */
void stress_yield_sleep_ms(void)
{
	const double t = stress_time_now();

	do {
		double duration;

		(void)shim_sched_yield();
		duration = stress_time_now() - t;
		if (duration > 0.001)
			break;
		(void)shim_usleep(1000);
	} while (stress_continue_flag());
}

#if defined(__linux__)
/*
 *  stress_process_info_dump()
 *	dump out /proc/$PID/filename data in human readable format
 */
static void stress_process_info_dump(
	stress_args_t *args,
	const pid_t pid,
	const char *filename)
{
	char path[4096];
	char buf[8192];
	char *ptr, *end, *begin, *emit;
	ssize_t ret;

	if (UNLIKELY(!filename))
		return;

	(void)snprintf(path, sizeof(path), "/proc/%" PRIdMAX "/%s", (intmax_t)pid, filename);
	ret = stress_system_read(path, buf, sizeof(buf));
	if (ret < 0)
		return;

	end = buf + ret;
	/* data like /proc/$PID/cmdline has '\0' chars - replace with spaces */
	for (ptr = buf; ptr < end; ptr++)
		if (*ptr == '\0')
			*ptr = ' ';

	ptr = buf;
	begin = ptr;
	emit = NULL;
	while (ptr < end) {
		while (ptr < end) {
			/* new line or eos, flush */
			if (*ptr == '\n' || *ptr == '\0') {
				*ptr = '\0';
				emit = begin;
				ptr++;
				begin = ptr;
			}
			ptr++;
			/* reached end, flush residual data out */
			if (ptr == end)
				emit = begin;
			if (emit) {
				pr_dbg("%s: [%" PRIdMAX "] %s: %s\n", args ? args->name : "main", (intmax_t)pid, filename, emit);
				emit = NULL;
			}
		}
	}
}
#endif

/*
 *  stress_process_info()
 *	dump out process specific debug from /proc
 */
void stress_process_info(stress_args_t *args, const pid_t pid)
{
#if defined(__linux__)
	pr_block_begin();
	stress_process_info_dump(args, pid, "cmdline");
	stress_process_info_dump(args, pid, "syscall");
	stress_process_info_dump(args, pid, "stack");
	stress_process_info_dump(args, pid, "wchan");
	pr_block_end();
#else
	(void)args;
	(void)pid;
#endif
}

/*
 *  stress_get_machine_id()
 *	try to get a unique 64 bit machine id number
 */
uint64_t stress_get_machine_id(void)
{
	uint64_t id = 0;

#if defined(__linux__)
	{
		char buf[17];

		/* Try machine id from /etc */
		if (stress_system_read("/etc/machine-id", buf, sizeof(buf)) > 0) {
			buf[16] = '\0';
			return (uint64_t)strtoll(buf, NULL, 16);
		}
	}
#endif
#if defined(__linux__)
	{
		char buf[17];

		/* Try machine id from /var/lib */
		if (stress_system_read("/var/lib/dbus/machine-id", buf, sizeof(buf)) > 0) {
			buf[16] = '\0';
			return (uint64_t)strtoll(buf, NULL, 16);
		}
	}
#endif
#if defined(HAVE_GETHOSTID)
	{
		/* Mangle 32 bit hostid to 64 bit */
		uint64_t hostid = (uint64_t)gethostid();

		id = hostid ^ ((~hostid) << 32);
	}
#endif
#if defined(HAVE_GETHOSTNAME)
	{
		char buf[256];

		/* Mangle hostname to 64 bit value */
		if (gethostname(buf, sizeof(buf)) == 0) {
			id ^= stress_hash_crc32c(buf) |
			      ((uint64_t)stress_hash_x17(buf) << 32);
		}
	}
#endif
	return id;
}

/*
 *  stress_zero_metrics()
 *	initialize metrics array 0..n-1 items
 */
void stress_zero_metrics(stress_metrics_t *metrics, const size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		metrics[i].lock = NULL;
		metrics[i].duration = 0.0;
		metrics[i].count = 0.0;
		metrics[i].t_start = 0.0;
	}
}

/*
 *  stress_data_is_not_zero()
 *	checks if buffer is zero, buffer must be 128 bit aligned
 */
bool OPTIMIZE3 stress_data_is_not_zero(uint64_t *buffer, const size_t len)
{
	register const uint64_t *end64 = buffer + (len / sizeof(uint64_t));
	register uint64_t *ptr64;
	register const uint8_t *end8;
	register uint8_t *ptr8;

PRAGMA_UNROLL_N(8)
	for (ptr64 = buffer; ptr64 < end64; ptr64++) {
		if (UNLIKELY(*ptr64))
			return true;
	}

	end8 = ((uint8_t *)buffer) + len;
PRAGMA_UNROLL_N(8)
	for (ptr8 = (uint8_t *)ptr64; ptr8 < end8; ptr8++) {
		if (UNLIKELY(*ptr8))
			return true;
	}
	return false;
}

/*
 *  stress_no_return
 *	function that does not return
 */
void stress_no_return(void)
{
	_exit(EXIT_FAILURE);
}
