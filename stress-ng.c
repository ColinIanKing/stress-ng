/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King
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
#include "core-affinity.h"
#include "core-attribute.h"
#include "core-bitops.h"
#include "core-builtin.h"
#include "core-clocksource.h"
#include "core-cpuidle.h"
#include "core-config-check.h"
#include "core-ftrace.h"
#include "core-hash.h"
#include "core-ignite-cpu.h"
#include "core-interrupts.h"
#include "core-io-priority.h"
#include "core-job.h"
#include "core-klog.h"
#include "core-limit.h"
#include "core-mlock.h"
#include "core-mmap.h"
#include "core-numa.h"
#include "core-opts.h"
#include "core-out-of-memory.h"
#include "core-perf.h"
#include "core-pragma.h"
#include "core-rapl.h"
#include "core-resctrl.h"
#include "core-shared-cache.h"
#include "core-shared-heap.h"
#include "core-smart.h"
#include "core-stressors.h"
#include "core-syslog.h"
#include "core-thermal-zone.h"
#include "core-thrash.h"
#include "core-vmstat.h"

#include <ctype.h>
#include <sched.h>
#include <time.h>
#include <math.h>
#include <float.h>

#include <sys/times.h>

#if defined(HAVE_SYS_UTSNAME_H)
#include <sys/utsname.h>
#endif

#if defined(HAVE_SYSLOG_H)
#include <syslog.h>
#endif

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

#define MIN_SEQUENTIAL		(0)
#define MAX_SEQUENTIAL		(1000000)
#define DEFAULT_SEQUENTIAL	(0)	/* Disabled */
#define DEFAULT_PARALLEL	(0)	/* Disabled */
#define DEFAULT_TIMEOUT		(60 * 60 * 24)
#define DEFAULT_BACKOFF		(0)
#define DEFAULT_CACHE_LEVEL     (3)

#define STRESS_REPORT_EXIT_SIGNALED		(1)

/* stress_stressor_info ignore value. 2 bits */
#define STRESS_STRESSOR_NOT_IGNORED		(0)
#define STRESS_STRESSOR_UNSUPPORTED		(1)
#define STRESS_STRESSOR_EXCLUDED		(2)

typedef void (*stress_sighandler_t)(int signum);

/* Map signals to handlers */
typedef struct stress_signal_map {
	int signum;			/* signal number */
	stress_sighandler_t handler;	/* signal handler */
} stress_signal_map_t;

/* Stress test classes */
typedef struct {
	const stress_class_t classifier;/* Class type bit mask */
	const char *name;		/* Name of class */
} stress_class_info_t;

typedef struct {
	const int opt;			/* optarg option */
	const uint64_t opt_flag;	/* global options flag bit setting */
} stress_opt_flag_t;

/* Per stressor linked list */
typedef struct {
	stress_stressor_t *head;
	stress_stressor_t *tail;
} stress_stressor_list_t;

static stress_stressor_list_t stress_stressor_list;

/* Various option settings and flags */
static volatile bool wait_flag = true;		/* false = exit run wait loop */
static pid_t main_pid;				/* stress-ng main pid */
static bool *sigalarmed = NULL;			/* pointer to stressor stats->sigalarmed */

/* Globals */
stress_stressor_t *g_stressor_current;		/* current stressor being invoked */
int32_t g_opt_sequential = DEFAULT_SEQUENTIAL;	/* # of sequential stressors */
int32_t g_opt_parallel = DEFAULT_PARALLEL;	/* # of parallel stressors */
int32_t g_opt_permute = DEFAULT_PARALLEL;	/* # of permuted stressors */
uint64_t g_opt_timeout = TIMEOUT_NOT_SET;	/* timeout in seconds */
uint64_t g_opt_flags = OPT_FLAGS_PR_ERROR |	/* default option flags */
		       OPT_FLAGS_PR_INFO |
		       OPT_FLAGS_MMAP_MADVISE;
unsigned int g_opt_pause = 0;			/* pause between stressor invocations */
volatile bool g_stress_continue_flag = true;	/* false to exit stressor */
const char g_app_name[] = "stress-ng";		/* Name of application */
stress_shared_t *g_shared;			/* shared memory */
jmp_buf g_error_env;				/* parsing error env */
stress_put_val_t g_put_val;			/* sync data to somewhere */
void *g_nowt = NULL;				/* used by thread returns */

#if defined(SA_SIGINFO)
typedef struct {
	int	code;				/* signal code */
	pid_t	pid;				/* PID of signalled process */
	uid_t	uid;				/* UID of signalled process */
	struct timeval when;			/* When signal occurred */
	bool 	triggered;			/* true when signal handled */
} stress_sigalrm_info_t;

static stress_sigalrm_info_t sigalrm_info;
#endif

/*
 *  optarg option to global setting option flags
 */
static const stress_opt_flag_t opt_flags[] = {
	{ OPT_abort,		OPT_FLAGS_ABORT },
	{ OPT_aggressive,	OPT_FLAGS_AGGRESSIVE_MASK },
	{ OPT_autogroup,	OPT_FLAGS_AUTOGROUP },
	{ OPT_buildinfo,	OPT_FLAGS_BUILDINFO },
	{ OPT_c_states,		OPT_FLAGS_C_STATES },
	{ OPT_change_cpu,	OPT_FLAGS_CHANGE_CPU },
	{ OPT_dry_run,		OPT_FLAGS_DRY_RUN },
	{ OPT_ftrace,		OPT_FLAGS_FTRACE },
	{ OPT_ignite_cpu,	OPT_FLAGS_IGNITE_CPU },
	{ OPT_interrupts,	OPT_FLAGS_INTERRUPTS },
	{ OPT_keep_files, 	OPT_FLAGS_KEEP_FILES },
	{ OPT_keep_name, 	OPT_FLAGS_KEEP_NAME },
	{ OPT_klog_check,	OPT_FLAGS_KLOG_CHECK },
	{ OPT_ksm,		OPT_FLAGS_KSM },
	{ OPT_log_brief,	OPT_FLAGS_LOG_BRIEF },
	{ OPT_log_lockless,	OPT_FLAGS_LOG_LOCKLESS },
	{ OPT_maximize,		OPT_FLAGS_MAXIMIZE },
	{ OPT_metrics,		OPT_FLAGS_METRICS | OPT_FLAGS_PR_METRICS },
	{ OPT_metrics_brief,	OPT_FLAGS_METRICS_BRIEF | OPT_FLAGS_METRICS | OPT_FLAGS_PR_METRICS },
	{ OPT_minimize,		OPT_FLAGS_MINIMIZE },
	{ OPT_no_oom_adjust,	OPT_FLAGS_NO_OOM_ADJUST },
	{ OPT_no_rand_seed,	OPT_FLAGS_NO_RAND_SEED },
	{ OPT_oomable,		OPT_FLAGS_OOMABLE },
	{ OPT_oom_no_child,	OPT_FLAGS_OOM_NO_CHILD },
	{ OPT_oom_avoid,	OPT_FLAGS_OOM_AVOID },
	{ OPT_page_in,		OPT_FLAGS_MMAP_MINCORE },
	{ OPT_pathological,	OPT_FLAGS_PATHOLOGICAL },
#if defined(STRESS_PERF_STATS) && 	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	{ OPT_perf_stats,	OPT_FLAGS_PERF_STATS },
#endif
	{ OPT_progress,		OPT_FLAGS_PROGRESS },
	{ OPT_randprocname,	OPT_FLAGS_RANDPROCNAME },
	{ OPT_rapl,		OPT_FLAGS_RAPL | OPT_FLAGS_RAPL_REQUIRED },
	{ OPT_settings,		OPT_FLAGS_SETTINGS },
	{ OPT_skip_silent,	OPT_FLAGS_SKIP_SILENT },
	{ OPT_smart,		OPT_FLAGS_SMART },
	{ OPT_sn,		OPT_FLAGS_SN },
	{ OPT_sock_nodelay,	OPT_FLAGS_SOCKET_NODELAY },
	{ OPT_stderr,		OPT_FLAGS_STDERR },
	{ OPT_stdout,		OPT_FLAGS_STDOUT },
	{ OPT_stressor_time,	OPT_FLAGS_STRESSOR_TIME },
	{ OPT_sync_start,	OPT_FLAGS_SYNC_START },
#if defined(HAVE_SYSLOG_H)
	{ OPT_syslog,		OPT_FLAGS_SYSLOG },
#endif
	{ OPT_taskset_random,	OPT_FLAGS_TASKSET_RANDOM },
	{ OPT_thrash, 		OPT_FLAGS_THRASH },
	{ OPT_times,		OPT_FLAGS_TIMES },
	{ OPT_timestamp,	OPT_FLAGS_TIMESTAMP },
	{ OPT_thermal_zones,	OPT_FLAGS_THERMAL_ZONES | OPT_FLAGS_TZ_INFO },
	{ OPT_verbose,		OPT_FLAGS_PR_ALL },
	{ OPT_verify,		OPT_FLAGS_VERIFY | OPT_FLAGS_PR_FAIL },
};

static void MLOCKED_TEXT stress_handle_terminate(int signum);

static const stress_signal_map_t stress_signal_map[] = {
	/* POSIX.1-1990 */
#if defined(SIGHUP)
	{ SIGHUP, 	stress_handle_terminate },
#endif
#if defined(SIGINT)
	{ SIGINT,	stress_handle_terminate },
#endif
#if defined(SIGILL)
	{ SIGILL,	stress_handle_terminate },
#endif
#if defined(SIGQUIT)
	{ SIGQUIT,	stress_handle_terminate },
#endif
#if defined(SIGABRT)
	{ SIGABRT,	stress_handle_terminate },
#endif
#if defined(SIGFPE)
	{ SIGFPE,	stress_handle_terminate },
#endif
#if defined(SIGSEGV)
	{ SIGSEGV,	stress_handle_terminate },
#endif
#if defined(SIGTERM)
	{ SIGTERM,	stress_handle_terminate },
#endif
#if defined(SIGXCPU)
	{ SIGXCPU,	stress_handle_terminate },
#endif
#if defined(SIGXFSZ)
	{ SIGXFSZ,	stress_handle_terminate },
#endif
	/* Linux various */
#if defined(SIGIOT)
	{ SIGIOT,	stress_handle_terminate },
#endif
#if defined(SIGSTKFLT)
	{ SIGSTKFLT,	stress_handle_terminate },
#endif
#if defined(SIGPWR)
	{ SIGPWR,	stress_handle_terminate },
#endif
#if defined(SIGINFO)
	{ SIGINFO,	stress_handle_terminate },
#endif
#if defined(SIGVTALRM)
	{ SIGVTALRM,	stress_handle_terminate },
#endif
#if defined(SIGUSR1)
	{ SIGUSR1,	SIG_IGN },
#endif
#if defined(SIGUSR2)
	{ SIGUSR2,	SIG_IGN },
#endif
#if defined(SIGTTOU)
	{ SIGTTOU,	SIG_IGN },
#endif
#if defined(SIGTTIN)
	{ SIGTTIN,	SIG_IGN },
#endif
#if defined(SIGWINCH)
	{ SIGWINCH,	SIG_IGN },
#endif
};

/* Stressor id values */
enum {
	STRESS_START = -1,
	STRESSORS(STRESSOR_ENUM)
};

/* Stressor extern info structs */
STRESSORS(STRESSOR_INFO)

/*
 *  Human readable stress test names, can't be const
 *  because name is munged to human readable form
 *  at start
 */
static stress_t stressors[] = {
	STRESSORS(STRESSOR_ELEM)
};

/*
 *  Different stress classes
 */
static const stress_class_info_t stress_classes[] = {
	{ CLASS_COMPUTE,	"compute" },
	{ CLASS_CPU_CACHE,	"cpu-cache" },
	{ CLASS_CPU,		"cpu" },
	{ CLASS_DEV,		"device" },
	{ CLASS_INTEGER,	"integer" },
	{ CLASS_FILESYSTEM,	"filesystem" },
	{ CLASS_FP,		"fp" },
	{ CLASS_GPU,		"gpu" },
	{ CLASS_INTERRUPT,	"interrupt" },
	{ CLASS_IO,		"io" },
	{ CLASS_IPC,		"ipc" },
	{ CLASS_MEMORY,		"memory" },
	{ CLASS_NETWORK,	"network" },
	{ CLASS_OS,		"os" },
	{ CLASS_PIPE_IO,	"pipe" },
	{ CLASS_SCHEDULER,	"scheduler" },
	{ CLASS_SEARCH,		"search" },
	{ CLASS_SECURITY,	"security" },
	{ CLASS_SIGNAL,		"signal" },
	{ CLASS_SORT,		"sort" },
	{ CLASS_VECTOR,		"vector" },
	{ CLASS_VM,		"vm" },
};

/*
 *  Generic help options
 */
static const stress_help_t help_generic[] = {
	{ NULL,		"abort",		"abort all stressors if any stressor fails" },
	{ NULL,		"aggressive",		"enable all aggressive options" },
	{ "a N",	"all N",		"start N workers of each stress test" },
	{ "b N",	"backoff N",		"wait of N microseconds before work starts" },
	{ NULL,		"change-cpu",		"force child processes to use different CPU to that of parent" },
	{ NULL,		"class name",		"specify a class of stressors, use with --sequential" },
	{ "n",		"dry-run",		"do not run" },
	{ NULL,		"ftrace",		"enable kernel function call tracing" },
	{ "h",		"help",			"show help" },
	{ NULL,		"ignite-cpu",		"alter kernel controls to make CPU run hot" },
	{ NULL,		"interrupts",		"check for error interrupts" },
	{ NULL,		"ionice-class C",	"specify ionice class (idle, besteffort, realtime)" },
	{ NULL,		"ionice-level L",	"specify ionice level (0 max, 7 min)" },
	{ "I",		"iostat S",		"show I/O statistics every S seconds" },
	{ "j",		"job jobfile",		"run the named jobfile" },
	{ NULL,		"keep-files",		"do not remove files or directories" },
	{ "k",		"keep-name",		"keep stress worker names to be 'stress-ng'" },
	{ "K",		"klog-check",		"check kernel message log for errors" },
	{ NULL,		"ksm",			"enable kernel samepage merging" },
	{ NULL,		"log-brief",		"less verbose log messages" },
	{ NULL,		"log-file filename",	"log messages to a log file" },
	{ NULL,		"log-lockless",		"log messages without message locking" },
	{ NULL,		"maximize",		"enable maximum stress options" },
	{ NULL,		"max-fd N",		"set maximum file descriptor limit" },
	{ NULL,		"mbind",		"set NUMA memory binding to specific nodes" },
	{ "M",		"metrics",		"print pseudo metrics of activity" },
	{ NULL,		"metrics-brief",	"enable metrics and only show non-zero results" },
	{ NULL,		"minimize",		"enable minimal stress options" },
	{ NULL,		"no-madvise",		"don't use random madvise options for each mmap" },
	{ NULL,		"no-oom-adjust",	"disable all forms of out-of-memory score adjustments" },
	{ NULL,		"no-rand-seed",		"seed random numbers with the same constant" },
	{ NULL,		"oom-avoid",		"Try to avoid stressors from being OOM'd" },
	{ NULL,		"oom-avoid-bytes N",	"Number of bytes free to stop further memory allocations" },
	{ NULL,		"oomable",		"Do not respawn a stressor if it gets OOM'd" },
	{ NULL,		"page-in",		"touch allocated pages that are not in core" },
	{ NULL,		"parallel N",		"synonym for 'all N'" },
	{ NULL,		"pathological",		"enable stressors that are known to hang a machine" },
#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	{ NULL,		"perf",			"display perf statistics" },
#endif
	{ NULL,		"permute N",		"run permutations of stressors with N stressors per permutation" },
	{ "q",		"quiet",		"quiet output" },
	{ "r",		"random N",		"start N random workers" },
	{ NULL,		"rapl",			"report RAPL power domain measurements over entire run (Linux x86 only)" },
	{ NULL,		"raplstat S",		"show RAPL power domain stats every S seconds (Linux x86 only)" },
	{ NULL,		"sched type",		"set scheduler type" },
	{ NULL,		"sched-prio N",		"set scheduler priority level N" },
	{ NULL,		"sched-period N",	"set period for SCHED_DEADLINE to N nanosecs (Linux only)" },
	{ NULL,		"sched-runtime N",	"set runtime for SCHED_DEADLINE to N nanosecs (Linux only)" },
	{ NULL,		"sched-deadline N",	"set deadline for SCHED_DEADLINE to N nanosecs (Linux only)" },
	{ NULL,		"sched-reclaim",        "set reclaim cpu bandwidth for deadline scheduler (Linux only)" },
	{ NULL,		"seed N",		"set the random number generator seed with a 64 bit value" },
	{ NULL,		"sequential N",		"run all stressors one by one, invoking N of them" },
	{ NULL,		"skip-silent",		"silently skip unimplemented stressors" },
	{ NULL,		"smart",		"show changes in S.M.A.R.T. data" },
	{ NULL,		"sn",			"use scientific notation for metrics" },
	{ NULL,		"status S",		"show stress-ng progress status every S seconds" },
	{ NULL,		"stderr",		"all output to stderr" },
	{ NULL,		"stdout",		"all output to stdout (now the default)" },
	{ NULL,		"stressor-time",	"log start and end run times of each stressor" },
	{ NULL,		"stressors",		"show available stress tests" },
#if defined(HAVE_SYSLOG_H)
	{ NULL,		"syslog",		"log messages to the syslog" },
#endif
	{ NULL,		"taskset",		"use specific CPUs (set CPU affinity)" },
	{ NULL,		"temp-path path",	"specify path for temporary directories and files" },
	{ NULL,		"thermalstat S",	"show CPU and thermal load stats every S seconds" },
	{ NULL,		"thrash",		"force all pages in causing swap thrashing" },
	{ "t N",	"timeout T",		"timeout after T seconds" },
	{ NULL,		"timer-slack N",	"set slack slack to N nanoseconds, 0 for default" },
	{ NULL,		"times",		"show run time summary at end of the run" },
	{ NULL,		"timestamp",		"timestamp log output " },
#if defined(STRESS_THERMAL_ZONES)
	{ NULL,		"tz",			"collect temperatures from thermal zones (Linux only)" },
#endif
	{ "v",		"verbose",		"verbose output" },
	{ NULL,		"verify",		"verify results (not available on all tests)" },
	{ NULL,		"verifiable",		"show stressors that enable verification via --verify" },
	{ "V",		"version",		"show version" },
	{ NULL,		"vmstat S",		"show memory and process statistics every S seconds" },
	{ NULL,		"vmstat-units U",	"vmstat memory units, one of k | m | g | t | p | e" },
	{ "x",		"exclude list",		"list of stressors to exclude (not run)" },
	{ "w",		"with list",		"list of stressors to invoke (use with --seq or --all)" },
	{ "Y",		"yaml file",		"output results to YAML formatted file" },
	{ NULL,		NULL,			NULL }
};

/*
 *  stress_hash_checksum()
 *	generate a hash of the checksum data
 */
static inline uint32_t stress_hash_checksum(stress_counter_info_t *ci)
{
	return stress_hash_jenkin((uint8_t *)ci, sizeof(*ci));
}

/*
 *  stress_ignore_stressor()
 *	remove stressor from stressor list
 */
static inline void stress_ignore_stressor(stress_stressor_t *ss, uint8_t reason)
{
	ss->ignore.run = reason;
}

/*
 *  stress_get_class_id()
 *	find the class id of a given class name
 */
static uint32_t PURE stress_get_class_id(const char *const str)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_classes); i++) {
		if (!strcmp(stress_classes[i].name, str))
			return stress_classes[i].classifier;
	}
	return 0;
}

/*
 *  stress_get_class()
 *	parse for allowed class types, return bit mask of types, 0 if error
 */
static int stress_get_class(char *const class_str, uint32_t *class)
{
	char *str, *token;
	int ret = 0;

	*class = 0;
	for (str = class_str; (token = strtok(str, ",")) != NULL; str = NULL) {
		uint32_t cl = stress_get_class_id(token);

		if (!cl) {
			size_t i;
			const size_t len = strlen(token);

			if ((len > 1) && (token[len - 1] == '?')) {
				token[len - 1] = '\0';

				cl = stress_get_class_id(token);
				if (cl) {
					size_t j;

					(void)printf("class '%s' stressors:",
						token);
					for (j = 0; j < SIZEOF_ARRAY(stressors); j++) {
						if (stressors[j].info->classifier & cl)
							(void)printf(" %s", stressors[j].name);
					}
					(void)printf("\n");
					return 1;
				}
			}
			(void)fprintf(stderr, "Unknown class: '%s', "
				"available classes:", token);
			for (i = 0; i < SIZEOF_ARRAY(stress_classes); i++)
				(void)fprintf(stderr, " %s", stress_classes[i].name);
			(void)fprintf(stderr, "\n\n");
			return -1;
		}
		*class |= cl;
	}
	return ret;
}

/*
 *  stress_stressor_find()
 *	return of stressor that matches
 *	the given stressor name, return -1;
 */
ssize_t stress_stressor_find(const char *name)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
		if (!stress_strcmp_munged(name, stressors[i].name))
			return (ssize_t)i;
	}
	return (ssize_t)-1;
}

/*
 *  stress_exclude()
 *  	parse -x --exlude exclude list
 */
static int stress_exclude(void)
{
	char *str, *token, *opt_exclude;

	if (!stress_get_setting("exclude", &opt_exclude))
		return 0;

	for (str = opt_exclude; (token = strtok(str, ",")) != NULL; str = NULL) {
		stress_stressor_t *ss;

		if (stress_stressor_find(token) < 0) {
			(void)fprintf(stderr, "exclude option specifies unknown stressor: '%s'\n", token);
			return -1;
		}
		for (ss = stress_stressor_list.head; ss; ss = ss->next) {
			if (!stress_strcmp_munged(token, ss->stressor->name)) {
				stress_ignore_stressor(ss, STRESS_STRESSOR_EXCLUDED);
				break;
			}
		}
	}
	return 0;
}

/*
 *  stress_zero_bogo_max_ops()
 *	zero'ing all the bogo_max_ops stops all stressors
 *	that are checking on stress_continue()
 */
void stress_zero_bogo_max_ops(void)
{
	stress_stressor_t *ss;

	for (ss = stress_stressor_list.head; ss; ss = ss->next) {
		int32_t i;

		if (!ss->ignore.run) {
			for (i = 0; i < ss->instances; i++)
				ss->stats[i]->args.bogo.max_ops = 0;
		}
	}
}

/*
 *  stress_kill_stressors()
 * 	kill stressor tasks using signal sig
 */
static void stress_kill_stressors(const int sig, const bool force_sigkill)
{
	int signum = sig;
	stress_stressor_t *ss;

	if (force_sigkill) {
		static int count = 0;
		static double kill_last = 0.0;

		/* multiple calls will always fallback to SIGKILL */
		if (getpid() == main_pid) {
			double t_now = stress_time_now();
			double t_delta = t_now - kill_last;

			/* Throttle spammy messages to 1/10th second */
			kill_last = t_now;
			if (t_delta > 0.10) {
				const uint32_t total = g_shared->instance_count.started +
						       g_shared->instance_count.exited;

				if (count == 0) {
					pr_inf("stopping %" PRIu32 " stressors\n", total);
				} else {
					pr_inf("stopping %" PRIu32 " of %" PRIu32 " stressors (%" PRIu32 " terminated)%s\n",
						g_shared->instance_count.started,
						total,
						g_shared->instance_count.exited,
						(count > 5) ? ", please be patient" : "");
				}
			}
		}
		if (count++ > 5)
			signum = SIGKILL;
	}

	for (ss = stress_stressor_list.head; ss; ss = ss->next) {
		int32_t i;

		if (ss->ignore.run)
			continue;

		for (i = 0; i < ss->instances; i++) {
			stress_stats_t *const stats = ss->stats[i];
			const pid_t pid = stats->s_pid.pid;

			/* Don't kill -1 (group), or init processes! */
			if ((pid > 1) && !stats->signalled) {
				(void)shim_kill(pid, signum);
				stats->signalled = true;
			}
		}
	}
}

/*
 *  stress_sigint_handler()
 *	catch signals and set flag to break out of stress loops
 */
static void MLOCKED_TEXT stress_sigint_handler(int signum)
{
	(void)signum;
	if (g_shared)
		g_shared->caught_sigint = true;
	stress_continue_set_flag(false);
	wait_flag = false;

	/* Send alarm to all stressors */
	stress_kill_stressors(SIGALRM, true);
}

/*
 *  stress_sigalrm_handler()
 *	handle signal in parent process, don't block on waits
 */
static void MLOCKED_TEXT stress_sigalrm_handler(int signum)
{
	if (g_shared) {
		g_shared->caught_sigint = true;
		if ((sigalarmed) && (!*sigalarmed)) {
			g_shared->instance_count.alarmed++;
			*sigalarmed = true;
		}
	}
	stress_zero_bogo_max_ops();

	if (getpid() == main_pid) {
		/* Parent */
		wait_flag = false;
		stress_kill_stressors(SIGALRM, false);
	} else {
		/* Child */
		stress_handle_stop_stressing(signum);
	}
}

/*
 *  stress_block_signals()
 *	block signals
 */
static void stress_block_signals(void)
{
	sigset_t set;

	(void)sigfillset(&set);
	(void)sigprocmask(SIG_SETMASK, &set, NULL);
}

#if defined(SA_SIGINFO)
static void MLOCKED_TEXT stress_sigalrm_action_handler(
	int signum,
	siginfo_t *info,
	void *ucontext)
{
	(void)ucontext;

	if (g_shared && 			/* shared mem initialized */
	    !g_shared->caught_sigint &&		/* and SIGINT not already handled */
	    info && 				/* and info is valid */
	    (info->si_code == SI_USER) &&	/* and not from kernel SIGALRM */
	    (!sigalrm_info.triggered)) {	/* and not already handled */
		sigalrm_info.code = info->si_code;
		sigalrm_info.pid = info->si_pid;
		sigalrm_info.uid = info->si_uid;
		(void)gettimeofday(&sigalrm_info.when, NULL);
		sigalrm_info.triggered = true;
	}
	stress_sigalrm_handler(signum);
}
#endif

#if defined(SIGUSR2)
/*
 *  stress_stats_handler()
 *	dump current system stats to stdout
 */
static void MLOCKED_TEXT stress_stats_handler(int signum)
{
	static char buffer[80];
	char *hdr = buffer;
	double min1, min5, min15;
	size_t shmall, freemem, totalmem, freeswap, totalswap;
	const int fd = pr_fd();
	int len = 0, ret;
#if defined(HAVE_ATOMIC_ADD_FETCH) &&	\
    defined(__ATOMIC_RELAXED)
	static int counter = 0;
#endif

	(void)signum;

	if (fd < 0)
		return;
#if defined(HAVE_ATOMIC_ADD_FETCH) &&	\
    defined(__ATOMIC_RELAXED)
	if (__atomic_add_fetch(&counter, 1, __ATOMIC_RELAXED) > 1)
		return;
#endif
	*hdr = '\0';
	ret = snprintf(buffer, sizeof(buffer), "%s: info:  [%" PRIdMAX "] ",
		g_app_name, (intmax_t)getpid());
	if (ret > 0) {
		hdr += ret;
		len += ret;
	}
	if (stress_get_load_avg(&min1, &min5, &min15) == 0) {
		ret = snprintf(hdr, sizeof(buffer) - len,
			"Load Average: %.2f %.2f %.2f\n",
			min1, min5, min15);
		if (ret > 0)
			VOID_RET(ssize_t, write(fd, buffer, len + ret));
	}
	stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);
	if ((totalmem > 0) || (freeswap > 0)) {
		ret = snprintf(hdr, sizeof(buffer) - len,
			"Mem Free: %zu MB, Mem Total: %zu MB\n",
			freemem / (size_t)MB, totalmem / (size_t)MB);
		if (ret > 0)
			VOID_RET(ssize_t, write(fd, buffer, len + ret));
	}
	if ((freeswap > 0) || (totalswap > 0)) {
		ret = snprintf(hdr, sizeof(buffer) - len,
			"Swap Free: %zu MB, Swap Total: %zu MB\n",
			freeswap / (size_t)MB, totalswap / (size_t)MB);
		if (ret > 0)
			VOID_RET(ssize_t, write(fd, buffer, len + ret));
	}
#if defined(HAVE_ATOMIC_ADD_FETCH) &&	\
    defined(__ATOMIC_RELAXED)
	(void)__atomic_sub_fetch(&counter, 1, __ATOMIC_RELAXED);
#endif
}
#endif

/*
 *  stress_set_handler()
 *	set signal handler to catch SIGINT, SIGALRM, SIGHUP
 */
static int stress_set_handler(const char *stress, const bool child)
{
#if defined(SA_SIGINFO)
	struct sigaction sa;
#endif
	if (stress_sighandler(stress, SIGINT, stress_sigint_handler, NULL) < 0)
		return -1;
	if (stress_sighandler(stress, SIGHUP, stress_sigint_handler, NULL) < 0)
		return -1;
#if defined(SIGUSR2)
	if (!child) {
		if (stress_sighandler(stress, SIGUSR2,
			stress_stats_handler, NULL) < 0) {
			return -1;
		}
	}
#endif
#if defined(SA_SIGINFO)
	(void)shim_memset(&sa, 0, sizeof(sa));
	(void)sigemptyset(&sa.sa_mask);
	/*
	 *  Signals intended to stop stress-ng should never be interrupted
	 *  by a signal with a handler which may not return to the caller.
	 */
	stress_mask_longjump_signals(&sa.sa_mask);
	sa.sa_sigaction = stress_sigalrm_action_handler;
	sa.sa_flags = SA_SIGINFO;
	if (sigaction(SIGALRM, &sa, NULL) < 0) {
		pr_fail("%s: sigaction SIGALRM failed, errno=%d (%s)\n",
                        stress, errno, strerror(errno));
	}
#else
	if (stress_sighandler(stress, SIGALRM, stress_sigalrm_handler, NULL) < 0)
		return -1;
#endif
	return 0;
}

/*
 *  stress_version()
 *	print program version info
 */
static void stress_version(void)
{
	(void)printf("%s, version " VERSION " (%s, %s)%s\n",
		g_app_name, stress_get_compiler(), stress_get_uname_info(),
		stress_is_dev_tty(STDOUT_FILENO) ? "" : " \U0001F4BB\U0001F525");
}

/*
 *  stress_usage_help()
 *	show generic help information
 */
static void stress_usage_help(const stress_help_t help_info[])
{
	size_t i;
	const int cols = stress_get_tty_width();

	for (i = 0; help_info[i].description; i++) {
		char opt_s[10] = "";
		int wd = 0;
		bool first = true;
		const char *ptr, *space = NULL;
		const char *start = help_info[i].description;

		if (help_info[i].opt_s)
			(void)snprintf(opt_s, sizeof(opt_s), "-%s,",
					help_info[i].opt_s);
		(void)printf("%-6s--%-22s", opt_s, help_info[i].opt_l);

		for (ptr = start; *ptr; ptr++) {
			if (*ptr == ' ')
				space = ptr;
			wd++;
			if (wd >= cols - 30) {
				const size_t n = (size_t)(space - start);

				if (!first)
					(void)printf("%-30s", "");
				first = false;
				(void)printf("%*.*s\n", (int)n, (int)n, start);
				start = space + 1;
				wd = 0;
			}
		}
		if (start != ptr) {
			const int n = (int)(ptr - start);

			if (!first)
				(void)printf("%-30s", "");
			(void)printf("%*.*s\n", n, n, start);
		}
	}
}

/*
 *  stress_verfiable_mode()
 *	show the stressors that are verified by their verify mode
 */
static void stress_verifiable_mode(const stress_verify_t mode)
{
	size_t i;
	bool space = false;

	for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
		if (stressors[i].info->verify == mode) {
			(void)printf("%s%s", space ? " " : "", stressors[i].name);
			space = true;
		}
	}
	(void)putchar('\n');
}

/*
 *  stress_verfiable()
 *	show the stressors that have --verify ability
 */
static void stress_verifiable(void)
{
	(void)printf("Verification always enabled:\n");
	stress_verifiable_mode(VERIFY_ALWAYS);
	(void)printf("\nVerification enabled by --verify option:\n");
	stress_verifiable_mode(VERIFY_OPTIONAL);
	(void)printf("\nVerification not implemented:\n");
	stress_verifiable_mode(VERIFY_NONE);
}

/*
 *  stress_usage_help_stressors()
 *	show per stressor help information
 */
static void stress_usage_help_stressors(void)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
		if (stressors[i].info->help)
			stress_usage_help(stressors[i].info->help);
	}
}

/*
 *  stress_show_stressor_names()
 *	show stressor names
 */
static inline void stress_show_stressor_names(void)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stressors); i++)
		(void)printf("%s%s", i ? " " : "", stressors[i].name);
	(void)putchar('\n');
}

/*
 *  stress_usage()
 *	print some help
 */
static void NORETURN stress_usage(void)
{
	stress_version();
	(void)printf("\nUsage: %s [OPTION [ARG]]\n", g_app_name);
	(void)printf("\nGeneral control options:\n");
	stress_usage_help(help_generic);
	(void)printf("\nStressor specific options:\n");
	stress_usage_help_stressors();
	(void)printf("\nExample: %s --cpu 8 --iomix 4 --vm 2 --vm-bytes 128M "
		"--fork 4 --timeout 10s\n\n"
		"Note: sizes can be suffixed with B, K, M, G and times with "
		"s, m, h, d, y\n", g_app_name);
	stress_settings_free();
	exit(EXIT_SUCCESS);
}

/*
 *  stress_opt_name()
 *	find name associated with an option value
 */
static const char PURE *stress_opt_name(const int opt_val)
{
	size_t i;

	for (i = 0; stress_long_options[i].name; i++)
		if (stress_long_options[i].val == opt_val)
			return stress_long_options[i].name;

	return "unknown";
}

/*
 *  stress_get_processors()
 *	get number of processors, set count if <=0 as:
 *		count = 0 -> number of CPUs in system
 *		count < 0 -> number of CPUs online
 */
static void stress_get_processors(int32_t *count)
{
	if (*count == 0)
		*count = stress_get_processors_configured();
	else if (*count < 0)
		*count = stress_get_processors_online();
}

/*
 *  stress_stressor_finished()
 *	mark a stressor process as complete
 */
static inline void stress_stressor_finished(pid_t *pid)
{
	*pid = 0;
	g_shared->instance_count.reaped++;
}

/*
 *  stress_exit_status_to_string()
 *	map stress-ng exit status returns into text
 */
static const char * PURE stress_exit_status_to_string(const int status)
{
	typedef struct {
		const int status;		/* exit status */
		const char *description;	/* exit description */
	} stress_exit_status_map_t;

	static const stress_exit_status_map_t stress_exit_status_map[] = {
		{ EXIT_SUCCESS,			"success" },
		{ EXIT_FAILURE,			"stress-ng core failure " },
		{ EXIT_NOT_SUCCESS,		"stressor failed" },
		{ EXIT_NO_RESOURCE,		"no resources" },
		{ EXIT_NOT_IMPLEMENTED,		"not implemented" },
		{ EXIT_SIGNALED,		"killed by signal" },
		{ EXIT_BY_SYS_EXIT,		"stressor terminated using _exit()" },
		{ EXIT_METRICS_UNTRUSTWORTHY,	"metrics may be untrustworthy" },
	};
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_exit_status_map); i++) {
		if (status == stress_exit_status_map[i].status)
			return stress_exit_status_map[i].description;
	}
	return "unknown";
}

/*
 *   stress_wait_pid()
 *	wait for a stressor by their given pid
 */
static void stress_wait_pid(
	stress_stressor_t *ss,
	const pid_t pid,
	stress_stats_t *stats,
	bool *success,
	bool *resource_success,
	bool *metrics_success,
	const int flag)
{
	pid_t ret;
	int status;
	bool do_abort = false;
	const char *name = ss->stressor->name;

	/* already reaped, don't bother waiting */
	if (stats->s_pid.reaped)
		return;
redo:
	ret = shim_waitpid(pid, &status, flag);
	if (ret > 0) {
		int wexit_status = WEXITSTATUS(status);

		stats->s_pid.reaped = true;

		if (WIFSIGNALED(status)) {
#if defined(WTERMSIG)
			const int wterm_signal = WTERMSIG(status);

			if (wterm_signal != SIGALRM) {
				const char *signame = stress_strsignal(wterm_signal);

				pr_dbg("%s: [%" PRIdMAX "] terminated on %s\n",
					name, (intmax_t)ret, signame);
			}
#else
			pr_dbg("%s [%" PRIdMAX "] terminated on signal\n",
				name, (intmax_t)ret);
#endif
			/*
			 *  If the stressor got killed by OOM or SIGKILL
			 *  then somebody outside of our control nuked it
			 *  so don't necessarily flag that up as a direct
			 *  failure.
			 */
			if (stress_process_oomed(ret)) {
				pr_dbg("%s: [%" PRIdMAX "] killed by the OOM killer\n",
					name, (intmax_t)ret);
			} else if (wterm_signal == SIGKILL) {
				pr_dbg("%s: [%" PRIdMAX "] possibly killed by the OOM killer\n",
					name, (intmax_t)ret);
			} else if (wterm_signal != SIGALRM) {
				*success = false;
				/* force EXIT_SIGNALED */
				wexit_status = EXIT_SIGNALED;
			}
		}
		switch (wexit_status) {
		case EXIT_SUCCESS:
			ss->status[STRESS_STRESSOR_STATUS_PASSED]++;
			break;
		case EXIT_NO_RESOURCE:
			ss->status[STRESS_STRESSOR_STATUS_SKIPPED]++;
			pr_warn_skip("%s: [%" PRIdMAX "] aborted early, no system resources\n",
				name, (intmax_t)ret);
			*resource_success = false;
			do_abort = true;
			break;
		case EXIT_NOT_IMPLEMENTED:
			ss->status[STRESS_STRESSOR_STATUS_SKIPPED]++;
			do_abort = true;
			break;
		case EXIT_SIGNALED:
			ss->status[STRESS_STRESSOR_STATUS_FAILED]++;
			do_abort = true;
			*success = false;
#if defined(STRESS_REPORT_EXIT_SIGNALED)
			pr_dbg("%s: [%" PRIdMAX "] aborted via a termination signal\n",
				name, (intmax_t)ret);
#endif
			break;
		case EXIT_BY_SYS_EXIT:
			ss->status[STRESS_STRESSOR_STATUS_FAILED]++;
			pr_dbg("%s: [%" PRIdMAX "] aborted via exit() which was not expected\n",
				name, (intmax_t)ret);
			do_abort = true;
			break;
		case EXIT_METRICS_UNTRUSTWORTHY:
			ss->status[STRESS_STRESSOR_STATUS_BAD_METRICS]++;
			*metrics_success = false;
			break;
		case EXIT_FAILURE:
			ss->status[STRESS_STRESSOR_STATUS_FAILED]++;
			/*
			 *  Stressors should really return EXIT_NOT_SUCCESS
			 *  as EXIT_FAILURE should indicate a core stress-ng
			 *  problem.
			 */
			wexit_status = EXIT_NOT_SUCCESS;
			goto wexit_status_default;
		default:
wexit_status_default:
			pr_err("%s: [%" PRIdMAX "] terminated with an error, exit status=%d (%s)\n",
				name, (intmax_t)ret, wexit_status,
				stress_exit_status_to_string(wexit_status));
			*success = false;
			do_abort = true;
			break;
		}
		if ((g_opt_flags & OPT_FLAGS_ABORT) && do_abort) {
			stress_continue_set_flag(false);
			wait_flag = false;
			stress_kill_stressors(SIGALRM, true);
		}

		stress_stressor_finished(&stats->s_pid.pid);
		pr_dbg("%s: [%" PRIdMAX "] terminated (%s)\n",
			name, (intmax_t)ret,
			stress_exit_status_to_string(wexit_status));
	} else if (ret == -1) {
		/* Somebody interrupted the wait */
		if (errno == EINTR)
			goto redo;
		/* This child did not exist, mark it done anyhow */
		if ((errno == ECHILD) || (errno == ESRCH))
			stress_stressor_finished(&stats->s_pid.pid);
	}
}

#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    NEED_GLIBC(2,3,0)
/*
 *  stress_wait_aggressive()
 *	while waiting for stressors to complete add some aggressive
 *	CPU affinity changing to exercise the scheduler placement
 */
static void stress_wait_aggressive(
	const int32_t ticks_per_sec,
	stress_stressor_t *stressors_list,
	bool *success,
	bool *resource_success,
	bool *metrics_success)
{
	stress_stressor_t *ss;
	cpu_set_t proc_mask;
	const useconds_t usec_sleep =
		ticks_per_sec ? 1000000 / ((useconds_t)ticks_per_sec) : 1000000 / 1000;

	pr_dbg("changing stressor cpu affinity every %lu usecs\n", (unsigned long int)usec_sleep);

	while (wait_flag) {
		const int32_t cpus = stress_get_processors_configured();
		bool procs_alive = false;

		/*
		 *  If we can't get the mask, then don't do
		 *  any affinity twiddling
		 */
		if (sched_getaffinity(0, sizeof(proc_mask), &proc_mask) < 0)
			return;
		if (!CPU_COUNT(&proc_mask))	/* Highly unlikely */
			return;

		(void)shim_usleep(usec_sleep);

		for (ss = stressors_list; ss; ss = ss->next) {
			int32_t j;

			if (ss->ignore.run || ss->ignore.permute)
				continue;

			for (j = 0; j < ss->instances; j++) {
				stress_stats_t *const stats = ss->stats[j];
				const pid_t pid = stats->s_pid.pid;

				if (pid && !stats->s_pid.reaped) {
					cpu_set_t mask;
					int32_t cpu_num;

					stress_wait_pid(ss, pid, stats,
						success, resource_success,
						metrics_success, WNOHANG);

					/* PID not reaped by the WNOHANG waitpid? */
					if (!stats->s_pid.reaped)
						procs_alive = true;
					do {
						cpu_num = (int32_t)stress_mwc32modn(cpus);
					} while (!(CPU_ISSET(cpu_num, &proc_mask)));

					CPU_ZERO(&mask);
					CPU_SET(cpu_num, &mask);

					/* may fail if child has just died, just continue */
					(void)sched_setaffinity(pid, sizeof(mask), &mask);
					(void)shim_sched_yield();
				}
			}
		}
		if (!procs_alive)
			break;
	}
}
#endif

/*
 *  stress_wait_stressors()
 * 	wait for stressor child processes
 */
static void stress_wait_stressors(
	stress_pid_t *s_pids_head,
	const int32_t ticks_per_sec,
	stress_stressor_t *stressors_list,
	bool *success,
	bool *resource_success,
	bool *metrics_success)
{
	stress_stressor_t *ss;

	stress_sync_start_cont_list(s_pids_head);

#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    NEED_GLIBC(2,3,0)
	/*
	 *  On systems that support changing CPU affinity
	 *  we keep on moving processes between processors
	 *  to impact on memory locality (e.g. NUMA) to
	 *  try to thrash the system when in aggressive mode
	 */
	if (g_opt_flags & (OPT_FLAGS_AGGRESSIVE | OPT_FLAGS_TASKSET_RANDOM))
		stress_wait_aggressive(ticks_per_sec, stressors_list, success, resource_success, metrics_success);
#else
	(void)ticks_per_sec;
#endif
	for (ss = stressors_list; ss; ss = ss->next) {
		int32_t j;

		if (ss->ignore.run || ss->ignore.permute)
			continue;

		for (j = 0; j < ss->instances; j++) {
			stress_stats_t *const stats = ss->stats[j];
			const pid_t pid = stats->s_pid.pid;

			if (pid) {
				const char *name = ss->stressor->name;

				stress_wait_pid(ss, pid, stats,
					success, resource_success, metrics_success, 0);
				stress_clean_dir(name, pid, (uint32_t)j);
			}
		}
	}
	if (g_opt_flags & OPT_FLAGS_IGNITE_CPU)
		stress_ignite_cpu_stop();
}

/*
 *  stress_handle_terminate()
 *	catch terminating signals
 */
static void MLOCKED_TEXT stress_handle_terminate(int signum)
{
	static char buf[128];
	const int fd = fileno(stderr);

	stress_continue_set_flag(false);

	switch (signum) {
	case SIGILL:
	case SIGSEGV:
	case SIGFPE:
	case SIGBUS:
	case SIGABRT:
		/*
		 *  Critical failure, report and die ASAP
		 */
		(void)snprintf(buf, sizeof(buf), "%s: info:  [%" PRIdMAX "] stressor terminated with unexpected %s\n",
			g_app_name, (intmax_t)getpid(), stress_strsignal(signum));
		VOID_RET(ssize_t, write(fd, buf, strlen(buf)));
		if (signum == SIGABRT)
			stress_backtrace();
		stress_kill_stressors(SIGALRM, true);
		_exit(EXIT_SIGNALED);
	default:
		/*
		 *  Kill stressors
		 */
		stress_kill_stressors(SIGALRM, true);
		break;
	}
}

/*
 *  stress_get_nth_stressor()
 *	return nth stressor from list
 */
static stress_stressor_t *stress_get_nth_stressor(const uint32_t n)
{
	stress_stressor_t *ss = stress_stressor_list.head;
	uint32_t i = 0;

	while (ss && (i < n)) {
		if (!ss->ignore.run)
			i++;
		ss = ss->next;
	}
	return ss;
}

/*
 *  stress_get_num_stressors()
 *	return number of stressors in stressor list
 */
static inline uint32_t stress_get_num_stressors(void)
{
	uint32_t n = 0;
	stress_stressor_t *ss;

	for (ss = stress_stressor_list.head; ss; ss = ss->next)
		if (!ss->ignore.run)
			n++;

	return n;
}

/*
 *  stress_stressors_free()
 *	free stressor info from stressor list
 */
static void stress_stressors_free(void)
{
	stress_stressor_t *ss = stress_stressor_list.head;

	while (ss) {
		stress_stressor_t *next = ss->next;

		free(ss->stats);
		free(ss);
		ss = next;
	}

	stress_stressor_list.head = NULL;
	stress_stressor_list.tail = NULL;
}

/*
 *  stress_get_total_instances()
 *	deterimin number of runnable stressors from list
 */
static int32_t stress_get_total_instances(stress_stressor_t *stressors_list)
{
	int32_t total_instances = 0;
	stress_stressor_t *ss;

	for (ss = stressors_list; ss; ss = ss->next)
		total_instances += ss->instances;

	return total_instances;
}

/*
 *  stress_child_atexit(void)
 *	handle unexpected exit() call in child stressor
 */
static void NORETURN stress_child_atexit(void)
{
	_exit(EXIT_BY_SYS_EXIT);
}

/*
 *  stress_metrics_set_const_check()
 *	set metrics with given description a value. If const_description is
 *	true then the description is a literal string and does not need
 *	to be dup'd from the shared memory heap, otherwise it's a stack
 *	based string and needs to be dup'd so it does not go out of scope.
 *
 *	Note that stress_shared_heap_dup_const will dup a string using
 *	special reserved shared heap that all stressors can access. The
 *	returned string must not be written to. It may even be a cached
 *	copy of another dup by another stressor process (to save memory).
 */
void stress_metrics_set_const_check(
	stress_args_t *args,
	const size_t idx,
	char *description,
	const bool const_description,
	const double value,
	const int mean_type)
{
	stress_metrics_data_t *metrics;
	stress_metrics_item_t *item;

	if (!args)
		return;

	metrics = args->metrics;
	if (!metrics)
		return;

	/* track max index requested */
	if (idx > metrics->max_metrics)
		metrics->max_metrics = idx;

	if (idx >= STRESS_MISC_METRICS_MAX)
		return;

	item = &metrics->items[idx];
	item->description = const_description ?
		description :
		stress_shared_heap_dup_const(description);
	if (item->description)
		item->value = value;
	metrics->items[idx].mean_type = mean_type;
}

#if defined(HAVE_GETRUSAGE)
/*
 *  stress_getrusage()
 *	accumulate rusgage stats
 */
static void stress_getrusage(const int who, stress_stats_t *stats)
{
	struct rusage usage;

	if (shim_getrusage(who, &usage) == 0) {
		stats->rusage_utime +=
			(double)usage.ru_utime.tv_sec +
			((double)usage.ru_utime.tv_usec) / STRESS_DBL_MICROSECOND;
		stats->rusage_stime +=
			(double)usage.ru_stime.tv_sec +
			((double)usage.ru_stime.tv_usec) / STRESS_DBL_MICROSECOND;
#if defined(HAVE_RUSAGE_RU_MAXRSS)
		if (stats->rusage_maxrss < usage.ru_maxrss)
			stats->rusage_maxrss = usage.ru_maxrss;
#else
		stats->rusage_maxrss = 0;	/* Not available */
#endif
	}
}
#endif

static void stress_get_usage_stats(const int32_t ticks_per_sec, stress_stats_t *stats)
{
#if defined(HAVE_GETRUSAGE)
	(void)ticks_per_sec;

	stats->rusage_utime = 0.0;
	stats->rusage_stime = 0.0;
	stress_getrusage(RUSAGE_SELF, stats);
	stress_getrusage(RUSAGE_CHILDREN, stats);
#else
	struct tms t;

	stats->rusage_utime = 0.0;
	stats->rusage_stime = 0.0;
	(void)shim_memset(&t, 0, sizeof(t));
	if ((ticks_per_sec > 0) && (times(&t) != (clock_t)-1)) {
		stats->rusage_utime =
			(double)(t.tms_utime + t.tms_cutime) / (double)ticks_per_sec;
		stats->rusage_stime =
			(double)(t.tms_stime + t.tms_cstime) / (double)ticks_per_sec;
	}
#endif
	stats->rusage_utime_total += stats->rusage_utime;
	stats->rusage_stime_total += stats->rusage_stime;
}

/*
 *  stress_log_time()
 *	log start/end of stressor run, name is stressor, whence is the time and
 *	tag is "start" or "finish".
 */
static void stress_log_time(const char *name, const double whence, const char *tag)
{
#if defined(HAVE_LOCALTIME_R)
	time_t t = (time_t)whence;
	struct tm tm;
	double fractional, integral;

	(void)localtime_r(&t, &tm);
	fractional = modf(whence, &integral) * 100.0;
	/* format stressor tag HH:MM:SS.HS YYYY:MM:DD */
	pr_dbg("%s: %s %2.2d:%2.2d:%2.2d.%2.0f %4.4d:%2.2d:%2.2d\n",
		name, tag, tm.tm_hour, tm.tm_min, tm.tm_sec, fractional,
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
#else
	/* fallback to Epoch time */
	pr_dbg("%s: %s %.2f (Epoch time)\n", name, tag, whence);
#endif
}

/*
 *  stress_run_child()
 *	invoke a stressor in a child process
 */
static int MLOCKED_TEXT stress_run_child(
	stress_checksum_t **checksum,
	stress_stats_t *const stats,
	const double fork_time_start,
	const int64_t backoff,
	const int32_t ticks_per_sec,
	const int32_t ionice_class,
	const int32_t ionice_level,
	const int32_t instance,
	const int32_t started_instances,
	const size_t page_size,
	const pid_t child_pid)
{
	const char *name = g_stressor_current->stressor->name;
	int rc = EXIT_SUCCESS;
	bool ok;
	double finish = 0.0, run_duration;
	stress_args_t *args;

	sigalarmed = &stats->sigalarmed;

	stress_set_proc_state(name, STRESS_STATE_START);
	g_shared->instance_count.started++;

	if (sched_settings_apply(true) < 0) {
		rc = EXIT_NO_RESOURCE;
		stress_block_signals();
		goto child_exit;
	}
	(void)atexit(stress_child_atexit);
	if (stress_set_handler(name, true) < 0) {
		rc = EXIT_FAILURE;
		stress_block_signals();
		goto child_exit;
	}
	stress_parent_died_alarm();
	stress_process_dumpable(false);
	stress_set_timer_slack();

	if (g_opt_flags & OPT_FLAGS_KSM)
		stress_ksm_memory_merge(1);

	stress_set_proc_state(name, STRESS_STATE_INIT);
	stress_mwc_reseed();
	stress_set_max_limits();
	stress_set_iopriority(ionice_class, ionice_level);
	(void)umask(0077);

	pr_dbg("%s: [%" PRIdMAX "] started (instance %" PRIu32 " on CPU %u)\n",
		name, (intmax_t)child_pid, instance, stress_get_cpu());

	if (g_opt_flags & OPT_FLAGS_INTERRUPTS)
		stress_interrupts_start(stats->interrupts);
#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	if (g_opt_flags & OPT_FLAGS_PERF_STATS)
		(void)stress_perf_open(&stats->sp);
#endif
	(void)shim_usleep((useconds_t)(backoff * started_instances));
#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	if (g_opt_flags & OPT_FLAGS_PERF_STATS)
		(void)stress_perf_enable(&stats->sp);
#endif
	stress_yield_sleep_ms();
	stats->start = stress_time_now();
	if (g_opt_timeout)
		(void)alarm((unsigned int)g_opt_timeout);
	args = &stats->args;
	if (stress_continue_flag() && !(g_opt_flags & OPT_FLAGS_DRY_RUN)) {
		const struct stressor_info *info = g_stressor_current->stressor->info;

		/* note: set args in same order as stress_args_t */
		args->bogo.max_ops = g_stressor_current->bogo_max_ops ?
			g_stressor_current->bogo_max_ops : NEVER_END_OPS;
		args->bogo.ci.counter = 0;
		args->bogo.possibly_oom_killed = false;
		args->name = name;
		args->instance = (uint32_t)instance;
		args->instances = (uint32_t)g_stressor_current->instances;
		args->pid = child_pid;
		args->page_size = page_size;
		args->time_end = g_opt_timeout ? stress_time_now() + (double)g_opt_timeout : DBL_MAX;
		args->mapped = &g_shared->mapped;
		args->metrics = &stats->metrics;
		args->stats = stats;
		args->info = info;

		if (instance == 0)
			stress_settings_dbg(args);
		stress_set_oom_adjustment(args, false);

		(void)shim_memset(*checksum, 0, sizeof(**checksum));
		stats->start = stress_time_now();
#if defined(STRESS_RAPL)
		if (g_opt_flags & OPT_FLAGS_RAPL)
			(void)stress_rapl_get_power_stressor(g_shared->rapl_domains, NULL);
#endif
		if (g_opt_flags & OPT_FLAGS_STRESSOR_TIME)
			stress_log_time(name, stats->start, "start");

		(void)stress_resctrl_set(name, instance, child_pid);

		rc = info->stressor(args);
		stress_sync_state_store(&stats->s_pid, STRESS_SYNC_START_FLAG_FINISHED);
		stress_block_signals();
		(void)alarm(0);
		if (g_opt_flags & OPT_FLAGS_INTERRUPTS) {
			stress_interrupts_stop(stats->interrupts);
			stress_interrupts_check_failure(name, stats->interrupts, instance, &rc);
		}
#if defined(STRESS_RAPL)
		if (g_opt_flags & OPT_FLAGS_RAPL)
			(void)stress_rapl_get_power_stressor(g_shared->rapl_domains, &stats->rapl);
#endif
		pr_fail_check(&rc);
#if defined(SA_SIGINFO) &&	\
    defined(SI_USER)
		/*
		 *  Sanity check if process was killed by
		 *  an external SIGALRM source
		 */
		if (sigalrm_info.triggered && (sigalrm_info.code == SI_USER)) {
			time_t t = sigalrm_info.when.tv_sec;
			const struct tm *tm = localtime(&t);

			if (tm) {
				pr_dbg("%s: terminated by SIGALRM externally at %2.2d:%2.2d:%2.2d.%2.2ld by user %" PRIdMAX "\n",
					name,
					tm->tm_hour, tm->tm_min, tm->tm_sec,
					(long int)sigalrm_info.when.tv_usec / 10000,
					(intmax_t)sigalrm_info.uid);
			} else {
				pr_dbg("%s: terminated by SIGALRM externally by user %" PRIdMAX "\n",
					name, (intmax_t)sigalrm_info.uid);
			}
		}
#endif
		stats->completed = true;
		ok = (rc == EXIT_SUCCESS);
		args->bogo.ci.run_ok = ok;
		(*checksum)->data.ci.run_ok = ok;
		/* Ensure reserved padding is zero to not confuse checksum */
		(void)shim_memset((*checksum)->data.pad, 0, sizeof((*checksum)->data.pad));

		stress_set_proc_state(name, STRESS_STATE_STOP);
		/*
		 *  Bogo ops counter should be OK for reading,
		 *  if not then flag up that the counter may
		 *  be untrustyworthy
		 */
		if ((!args->bogo.ci.counter_ready) && (!args->bogo.ci.force_killed)) {
			pr_warn("%s: WARNING: bogo-ops counter in non-ready state, "
				"metrics are untrustworthy (process may have been "
				"terminated prematurely)\n",
				name);
			rc = EXIT_METRICS_UNTRUSTWORTHY;
		}
		(*checksum)->data.ci.counter = args->bogo.ci.counter;
		(*checksum)->hash = stress_hash_checksum(&((*checksum)->data.ci));
		finish = stress_time_now();
		if (g_opt_flags & OPT_FLAGS_STRESSOR_TIME)
			stress_log_time(name, finish, "finish");
	}
#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	if (g_opt_flags & OPT_FLAGS_PERF_STATS) {
		(void)stress_perf_disable(&stats->sp);
		(void)stress_perf_close(&stats->sp);
	}
#endif
#if defined(STRESS_THERMAL_ZONES)
	if (g_opt_flags & OPT_FLAGS_THERMAL_ZONES)
		(void)stress_tz_get_temperatures(&g_shared->tz_info, &stats->tz);
#endif
	stats->duration = (finish > 0.0) ? finish - stats->start : 0.0;
	stats->counter_total += args->bogo.ci.counter;
	stats->duration_total += stats->duration;

	stress_get_usage_stats(ticks_per_sec, stats);
	pr_dbg("%s: [%" PRIdMAX "] exited (instance %" PRIu32 " on CPU %d)\n",
		name, (intmax_t)child_pid, instance, stress_get_cpu());

	/* Allow for some slops of ~0.5 secs */
	run_duration = (finish - fork_time_start) + 0.5;

	/*
	 * Apparently succeeded but terminated early?
	 * Could be a bug, so report a warning
	 */
	if (args->bogo.ci.run_ok &&
	    (g_shared && !g_shared->caught_sigint) &&
	    (run_duration < (double)g_opt_timeout) &&
	    (!(g_stressor_current->bogo_max_ops && args->bogo.ci.counter >= g_stressor_current->bogo_max_ops))) {
		pr_warn("%s: WARNING: finished prematurely after just %s\n",
			name, stress_duration_to_str(run_duration, true, true));
	}
child_exit:
	/*
	 *  We used to free allocations on the heap, but
	 *  the child is going to _exit() soon so it's
	 *  faster to just free the heap objects on _exit()
	 */
	if ((rc != 0) && (g_opt_flags & OPT_FLAGS_ABORT)) {
		stress_continue_set_flag(false);
		wait_flag = false;
		(void)shim_kill(getppid(), SIGALRM);
	}
	stress_set_proc_state(name, STRESS_STATE_EXIT);
	g_shared->instance_count.exited++;
	g_shared->instance_count.started--;
	if (rc == EXIT_FAILURE)
		g_shared->instance_count.failed++;

	return rc;
}

/*
 *  stress_run()
 *	kick off and run stressors
 */
static void MLOCKED_TEXT stress_run(
	const int32_t ticks_per_sec,
	stress_stressor_t *stressors_list,
	double *duration,
	bool *success,
	bool *resource_success,
	bool *metrics_success,
	stress_checksum_t **checksum)
{
	double time_start, time_finish;
	int32_t started_instances = 0;
	const size_t page_size = stress_get_page_size();
	int64_t backoff = DEFAULT_BACKOFF;
	int32_t ionice_class = UNDEFINED;
	int32_t ionice_level = UNDEFINED;
	bool handler_set = false;
	stress_pid_t *s_pids_head = NULL;

	wait_flag = true;
	time_start = stress_time_now();

	(void)stress_get_setting("backoff", &backoff);
	(void)stress_get_setting("ionice-class", &ionice_class);
	(void)stress_get_setting("ionice-level", &ionice_level);

	if (g_opt_pause) {
		static bool first_run = true;

		if (first_run)
			first_run = false;
		else {
			pr_dbg("pausing for %u second%s\n", g_opt_pause,
				g_opt_pause == 1 ? "" : "s");
			(void)sleep(g_opt_pause);
		}
	}
	pr_dbg("starting stressors\n");

	/*
	 *  Work through the list of stressors to run
	 */
	for (g_stressor_current = stressors_list; g_stressor_current; g_stressor_current = g_stressor_current->next) {
		int32_t j;

		if (g_stressor_current->ignore.run || g_stressor_current->ignore.permute) {
			*checksum += g_stressor_current->instances;
			continue;
		}

		/*
		 *  Each stressor has 1 or more instances to run
		 */
		for (j = 0; j < g_stressor_current->instances; j++, (*checksum)++) {
			double fork_time_start;
			pid_t pid, child_pid;
			int rc;
			stress_stats_t *const stats = g_stressor_current->stats[j];


#if defined(STRESS_TERMINATE_PREMATURELY)
			if (g_opt_timeout && (stress_time_now() - time_start > (double)g_opt_timeout))
				goto abort;
#endif
			stress_sync_start_init(&stats->s_pid);
			stats->args.bogo.ci.counter_ready = true;
			stats->args.bogo.ci.counter = 0;
			stats->checksum = *checksum;

			if (g_opt_flags & OPT_FLAGS_DRY_RUN) {
				stats->s_pid.reaped = true;
				stats->s_pid.pid = -1;
				continue;
			}
again:
			if (!stress_continue_flag())
				break;
			fork_time_start = stress_time_now();
			pid = fork();
			switch (pid) {
			case -1:
				stats->s_pid.reaped = true;
				if (errno == EAGAIN) {
					(void)shim_usleep(100000);
					goto again;
				}
				pr_err("cannot fork, errno=%d (%s)\n",
					errno, strerror(errno));
				stress_kill_stressors(SIGALRM, false);
				goto wait_for_stressors;
			case 0:
				/* Child */
				stress_set_proc_state(g_stressor_current->stressor->name, STRESS_STATE_INIT);
				child_pid = getpid();
				stats->s_pid.reaped = false;
				stats->s_pid.pid = child_pid;
				if (g_opt_flags & OPT_FLAGS_C_STATES)
					stress_cpuidle_read_cstates_begin(&stats->cstates);

				rc = stress_run_child(checksum,
						stats, fork_time_start,
						backoff, ticks_per_sec,
						ionice_class, ionice_level,
						j, started_instances,
						page_size, child_pid);
				if (g_opt_flags & OPT_FLAGS_C_STATES)
					stress_cpuidle_read_cstates_end(&stats->cstates);
				_exit(rc);
			default:
				if (pid > -1) {
					stats->s_pid.pid = pid;
					stats->s_pid.reaped = false;
					stats->signalled = false;
					started_instances++;
					stress_ftrace_add_pid(pid);

					stress_sync_start_s_pid_list_add(&s_pids_head, &stats->s_pid);
				}

				/* Forced early abort during startup? */
				if (!stress_continue_flag()) {
					pr_dbg("abort signal during startup, cleaning up\n");
					stress_kill_stressors(SIGALRM, true);
					goto wait_for_stressors;
				}
				break;
			}
		}
	}
	if (!handler_set) {
		(void)stress_set_handler("stress-ng", false);
		handler_set = true;
	}
#if defined(STRESS_TERMINATE_PREMATURELY)
abort:
#endif
	pr_dbg("%" PRId32 " stressor%s started\n", started_instances,
		 started_instances == 1 ? "" : "s");

wait_for_stressors:
	if (!handler_set)
		(void)stress_set_handler("stress-ng", false);
	if (g_opt_flags & OPT_FLAGS_IGNITE_CPU)
		stress_ignite_cpu_start();
#if STRESS_FORCE_TIMEOUT_ALL
	if (!(g_opt_flags & OPT_FLAGS_SYNC_START))
		stress_start_timeout();
#endif
	stress_wait_stressors(s_pids_head, ticks_per_sec, stressors_list, success, resource_success, metrics_success);
	time_finish = stress_time_now();

	*duration += time_finish - time_start;
}

/*
 *  stress_show_stressors()
 *	show names of stressors that are going to be run
 */
static int stress_show_stressors(void)
{
	char *newstr, *str = NULL;
	ssize_t len = 0;
	char buffer[64];
	bool previous = false;
	stress_stressor_t *ss;

	for (ss = stress_stressor_list.head; ss; ss = ss->next) {
		int32_t n;

		if (ss->ignore.run)
			continue;

		n = ss->instances;
		if (n) {
			const char *name = ss->stressor->name;
			ssize_t buffer_len;

			buffer_len = snprintf(buffer, sizeof(buffer),
					"%s %" PRId32 " %s", previous ? "," : "", n, name);
			previous = true;
			if (buffer_len >= 0) {
				newstr = realloc(str, (size_t)(len + buffer_len + 1));
				if (!newstr) {
					pr_err("cannot allocate %zu byte temporary buffer%s\n",
						(size_t)(len + buffer_len + 1),
						stress_get_memfree_str());
					free(str);
					return -1;
				}
				str = newstr;
				(void)shim_strscpy(str + len, buffer, (size_t)(buffer_len + 1));
			}
			len += buffer_len;
		}
	}
	pr_inf("dispatching hogs:%s\n", str ? str : "");
	free(str);

	return 0;
}

/*
 *  stress_exit_status_type()
 *	report exit status of all instances of a given status type
 */
static void stress_exit_status_type(const char *name, const size_t type)
{
	stress_stressor_t *ss;

	char *str;
	size_t str_len = 1;
	uint32_t n = 0;

	str = malloc(1);
	if (!str)
		return;
	*str = '\0';

	for (ss = stress_stressor_list.head; ss; ss = ss->next) {
		uint32_t count = ss->status[type];

		if ((ss->ignore.run) && (type == STRESS_STRESSOR_STATUS_SKIPPED)) {
			count = ss->instances;
		}
		if (count > 0) {
			char buf[80];
			char *new_str;
			size_t buf_len;

			(void)snprintf(buf, sizeof(buf), " %s (%" PRIu32")",
				ss->stressor->name, count);
			buf_len = strlen(buf);
			new_str = realloc(str, str_len + buf_len);
			if (!new_str) {
				free(str);
				return;
			}
			str = new_str;
			str_len += buf_len;
			(void)shim_strlcat(str, buf, str_len);
			n += count;
		}
	}
	if (n) {
		pr_inf("%s: %" PRIu32 ":%s\n", name, n, str);
	} else  {
		pr_inf("%s: 0\n", name);
	}
	free(str);
}

/*
 *  stress_exit_status_summary()
 *	provide summary of exit status of all instances
 */
static void stress_exit_status_summary(void)
{
	stress_exit_status_type("skipped", STRESS_STRESSOR_STATUS_SKIPPED);
	stress_exit_status_type("passed", STRESS_STRESSOR_STATUS_PASSED);
	stress_exit_status_type("failed", STRESS_STRESSOR_STATUS_FAILED);
	stress_exit_status_type("metrics untrustworthy", STRESS_STRESSOR_STATUS_BAD_METRICS);
}

/*
 *  stress_metrics_check()
 *	as per ELISA request, sanity check bogo ops and run flag
 *	to see if corruption occurred and print failure messages
 *	and set *success to false if hash and data is dubious.
 */
static void stress_metrics_check(bool *success)
{
	stress_stressor_t *ss;
	bool ok = true;
	uint64_t counter_check = 0;
	uint32_t instances = 0;
	double min_run_time = DBL_MAX;

	for (ss = stress_stressor_list.head; ss; ss = ss->next) {
		int32_t j;

		if (ss->ignore.run)
			continue;

		instances += ss->instances;

		for (j = 0; j < ss->instances; j++) {
			const stress_stats_t *const stats = ss->stats[j];
			const stress_checksum_t *checksum = stats->checksum;
			stress_checksum_t stats_checksum;
			char *oom_message;

			if (!stats->completed)
				continue;

			counter_check |= stats->args.bogo.ci.counter;
			if (stats->duration < min_run_time)
				min_run_time = stats->duration;

			if (checksum == NULL) {
				pr_fail("%s instance %" PRId32 " unexpected null checksum data\n",
					ss->stressor->name, j);
				ok = false;
				continue;
			}

			(void)shim_memset(&stats_checksum, 0, sizeof(stats_checksum));
			stats_checksum.data.ci.counter = stats->args.bogo.ci.counter;
			stats_checksum.data.ci.run_ok = stats->args.bogo.ci.run_ok;
			stats_checksum.hash = stress_hash_checksum(&stats_checksum.data.ci);

			oom_message = stats->args.bogo.possibly_oom_killed ?
				" (possibly terminated by out-of-memory killer)" : "";

			if (stats->args.bogo.ci.counter != checksum->data.ci.counter) {
				pr_fail("%s instance %" PRId32 " corrupted bogo-ops counter, %" PRIu64 " vs %" PRIu64 "%s\n",
					ss->stressor->name, j,
					stats->args.bogo.ci.counter, checksum->data.ci.counter,
					oom_message);
				oom_message = "";
				ok = false;
			}
			if (stats->args.bogo.ci.run_ok != checksum->data.ci.run_ok) {
				pr_fail("%s instance % " PRId32 " corrupted run flag, %d vs %d%s\n",
					ss->stressor->name, j,
					stats->args.bogo.ci.run_ok, checksum->data.ci.run_ok,
					oom_message);
				oom_message = "";
				ok = false;
			}
			if (stats_checksum.hash != checksum->hash) {
				pr_fail("%s instance %" PRId32 " hash error in bogo-ops counter and run flag, %" PRIu32 " vs %" PRIu32 "%s\n",
					ss->stressor->name, j,
					stats_checksum.hash, checksum->hash,
					oom_message);
				ok = false;
			}
		}
	}

	/*
	 *  No point sanity checking metrics if nothing happened
	 */
	if (instances == 0) {
		pr_dbg("metrics-check: no stressors run\n");
		return;
	}

	/*
	 *  Dry run, nothing happened
	 */
	if (g_opt_flags & OPT_FLAGS_DRY_RUN)
		return;

	/*
	 *  Bogo ops counter should be not zero for the majority of
	 *  stressors after 30 seconds of run time
	 */
	if (!counter_check && (min_run_time > 30.0))
		pr_warn("metrics-check: all bogo-op counters are zero, data may be incorrect\n");

	if (ok) {
		pr_dbg("metrics-check: all stressor metrics validated and sane\n");
	} else {
		pr_fail("metrics-check: stressor metrics corrupted, data is compromised\n");
		*success = false;
	}
}

static char *stress_description_yamlify(const char *description)
{
	static char yamlified[40];
	char *dst;
	const char *src, *end = yamlified + sizeof(yamlified);

	for (dst = yamlified, src = description; *src; src++) {
		register int ch = (int)*src;

		if (isalpha((unsigned char)ch)) {
			*(dst++) = (char)tolower((unsigned char)ch);
		} else if (isdigit((unsigned char)ch)) {
			*(dst++) = (char)ch;
		} else if (ch == ' ') {
			*(dst++) = '-';
		}
		if (dst >= end - 1)
			break;
	}
	*dst = '\0';

	return yamlified;
}

/*
 *  stress_metrics_dump()
 *	output metrics
 */
static void stress_metrics_dump(FILE *yaml)
{
	stress_stressor_t *ss;
	const stress_metrics_item_t *item;
	const char *description;
	bool misc_metrics = false;

	pr_block_begin();
	if (g_opt_flags & OPT_FLAGS_METRICS_BRIEF) {
		pr_metrics("%-13s %9.9s %9.9s %9.9s %9.9s %12s %14s\n",
			   "stressor", "bogo ops", "real time", "usr time",
			   "sys time", "bogo ops/s", "bogo ops/s");
		pr_metrics("%-13s %9.9s %9.9s %9.9s %9.9s %12s %14s\n",
			   "", "", "(secs) ", "(secs) ", "(secs) ", "(real time)",
			   "(usr+sys time)");
	} else {
		pr_metrics("%-13s %9.9s %9.9s %9.9s %9.9s %12s %14s %12.12s %13.13s\n",
			   "stressor", "bogo ops", "real time", "usr time",
			   "sys time", "bogo ops/s", "bogo ops/s", "CPU used per",
			   "RSS Max");
		pr_metrics("%-13s %9.9s %9.9s %9.9s %9.9s %12s %14s %12.12s %13.13s\n",
			   "", "", "(secs) ", "(secs) ", "(secs) ", "(real time)",
			   "(usr+sys time)","instance (%)", "(KB)");
	}
	pr_yaml(yaml, "metrics:\n");

	for (ss = stress_stressor_list.head; ss; ss = ss->next) {
		uint64_t c_total = 0;
		double   r_total = 0.0, u_total = 0.0, s_total = 0.0;
		long int maxrss = 0;
		int32_t  j;
		size_t i;
		const char *name;
		double u_time, s_time, t_time, bogo_rate_r_time, bogo_rate, cpu_usage;
		bool run_ok = false;

		if (ss->ignore.run || ss->ignore.permute)
			continue;
		if (!ss->stats)
			continue;

		name = ss->stressor->name;

		for (j = 0; j < ss->instances; j++)
			ss->completed_instances = 0;

		for (j = 0; j < ss->instances; j++) {
			const stress_stats_t *const stats = ss->stats[j];

			if (stats->completed)
				ss->completed_instances++;

			run_ok  |= stats->args.bogo.ci.run_ok;
			c_total += stats->counter_total;
			u_total += stats->rusage_utime_total;
			s_total += stats->rusage_stime_total;
#if defined(HAVE_RUSAGE_RU_MAXRSS)
			if (maxrss < stats->rusage_maxrss)
				maxrss = stats->rusage_maxrss;
#endif
			r_total += stats->duration_total;
		}
		/* Real time in terms of average wall clock time of all procs */
		r_total = ss->completed_instances ?
			r_total / (double)ss->completed_instances : 0.0;

		if ((g_opt_flags & OPT_FLAGS_METRICS_BRIEF) &&
		    (c_total == 0) && (!run_ok))
			continue;

		u_time = u_total;
		s_time = s_total;
		t_time = u_time + s_time;

		/* Total usr + sys time of all procs */
		bogo_rate_r_time = (r_total > 0.0) ? (double)c_total / r_total : 0.0;
		{
			double us_total = u_time + s_time;

			bogo_rate = (us_total > 0.0) ? (double)c_total / us_total : 0.0;
		}

		cpu_usage = (r_total > 0) ? 100.0 * t_time / r_total : 0.0;
		cpu_usage = ss->completed_instances ? cpu_usage / ss->completed_instances : 0.0;

		if (g_opt_flags & OPT_FLAGS_METRICS_BRIEF) {
			if (g_opt_flags & OPT_FLAGS_SN) {
				pr_metrics("%-13s %9" PRIu64 " %9.3e %9.3e %9.3e %12.5e %14.5e\n",
					name,		/* stress test name */
					c_total,	/* op count */
					r_total,	/* average real (wall) clock time */
					u_time, 	/* actual user time */
					s_time,		/* actual system time */
					bogo_rate_r_time, /* bogo ops on wall clock time */
					bogo_rate);	/* bogo ops per second */
			} else {
				pr_metrics("%-13s %9" PRIu64 " %9.2f %9.2f %9.2f %12.2f %14.2f\n",
					name,		/* stress test name */
					c_total,	/* op count */
					r_total,	/* average real (wall) clock time */
					u_time, 	/* actual user time */
					s_time,		/* actual system time */
					bogo_rate_r_time, /* bogo ops on wall clock time */
					bogo_rate);	/* bogo ops per second */
			}
		} else {
			/* extended metrics */
			if (g_opt_flags & OPT_FLAGS_SN) {
				pr_metrics("%-13s %9" PRIu64 " %9.3e %9.3e %9.3e %12.5e %14.5e %15.4e %13ld\n",
					name,		/* stress test name */
					c_total,	/* op count */
					r_total,	/* average real (wall) clock time */
					u_time, 	/* actual user time */
					s_time,		/* actual system time */
					bogo_rate_r_time, /* bogo ops on wall clock time */
					bogo_rate,	/* bogo ops per second */
					cpu_usage,	/* % cpu usage */
					maxrss);	/* maximum RSS in KB */
			} else {
				pr_metrics("%-13s %9" PRIu64 " %9.2f %9.2f %9.2f %12.2f %14.2f %12.2f %13ld\n",
					name,		/* stress test name */
					c_total,	/* op count */
					r_total,	/* average real (wall) clock time */
					u_time, 	/* actual user time */
					s_time,		/* actual system time */
					bogo_rate_r_time, /* bogo ops on wall clock time */
					bogo_rate,	/* bogo ops per second */
					cpu_usage,	/* % cpu usage */
					maxrss);	/* maximum RSS in KB */
			}
		}

		if (g_opt_flags & OPT_FLAGS_SN) {
			pr_yaml(yaml, "    - stressor: %s\n", name);
			pr_yaml(yaml, "      bogo-ops: %" PRIu64 "\n", c_total);
			pr_yaml(yaml, "      bogo-ops-per-second-usr-sys-time: %e\n", bogo_rate);
			pr_yaml(yaml, "      bogo-ops-per-second-real-time: %e\n", bogo_rate_r_time);
			pr_yaml(yaml, "      wall-clock-time: %e\n", r_total);
			pr_yaml(yaml, "      user-time: %e\n", u_time);
			pr_yaml(yaml, "      system-time: %e\n", s_time);
			pr_yaml(yaml, "      cpu-usage-per-instance: %e\n", cpu_usage);
			pr_yaml(yaml, "      max-rss: %ld\n", maxrss);
		} else {
			pr_yaml(yaml, "    - stressor: %s\n", name);
			pr_yaml(yaml, "      bogo-ops: %" PRIu64 "\n", c_total);
			pr_yaml(yaml, "      bogo-ops-per-second-usr-sys-time: %f\n", bogo_rate);
			pr_yaml(yaml, "      bogo-ops-per-second-real-time: %f\n", bogo_rate_r_time);
			pr_yaml(yaml, "      wall-clock-time: %f\n", r_total);
			pr_yaml(yaml, "      user-time: %f\n", u_time);
			pr_yaml(yaml, "      system-time: %f\n", s_time);
			pr_yaml(yaml, "      cpu-usage-per-instance: %f\n", cpu_usage);
			pr_yaml(yaml, "      max-rss: %ld\n", maxrss);
		}

		for (i = 0; i < SIZEOF_ARRAY(ss->stats[0]->metrics.items); i++) {
			item = &ss->stats[0]->metrics.items[i];
			description = item->description;

			if (description) {
				double metric, total = 0.0;

				misc_metrics = true;
				for (j = 0; j < ss->instances; j++) {
					const stress_stats_t *const stats = ss->stats[j];

					total += stats->metrics.items[i].value;
				}
				metric = ss->completed_instances ? total / ss->completed_instances : 0.0;
				if (g_opt_flags & OPT_FLAGS_SN) {
					pr_yaml(yaml, "      %s: %e\n", stress_description_yamlify(description), metric);
				} else {
					pr_yaml(yaml, "      %s: %f\n", stress_description_yamlify(description), metric);
				}
			}
		}
		pr_yaml(yaml, "\n");
	}

	if (misc_metrics && !(g_opt_flags & OPT_FLAGS_METRICS_BRIEF)) {
		pr_metrics("miscellaneous metrics:\n");
		for (ss = stress_stressor_list.head; ss; ss = ss->next) {
			size_t i;
			int32_t j;
			const char *name;

			if (ss->ignore.run)
				continue;
			if (!ss->stats)
				continue;

			name = ss->stressor->name;
			if (ss->stats[0]->metrics.max_metrics > SIZEOF_ARRAY(ss->stats[0]->metrics.items))
				pr_metrics("note: %zd metrics were set, only reporting first %zd metrics\n",
					ss->stats[0]->metrics.max_metrics, SIZEOF_ARRAY(ss->stats[0]->metrics.items));

			for (i = 0; i < SIZEOF_ARRAY(ss->stats[0]->metrics.items); i++) {
				item = &ss->stats[0]->metrics.items[i];
				description = item->description;

				if (description) {
					int64_t exponent;
					double geometric_mean, harmonic_mean, mantissa;
					double n, sum, maximum = 0.0, total = 0.0;
					const char *plural = (ss->completed_instances > 1) ? "s" : "";

					switch (ss->stats[0]->metrics.items[i].mean_type) {
					case STRESS_METRIC_GEOMETRIC_MEAN:
						exponent = 0;
						mantissa = 1.0;
						n = 0.0;

						for (j = 0; j < ss->instances; j++) {
							int e;
							const stress_stats_t *const stats = ss->stats[j];

							item = &stats->metrics.items[i];
							if ((item->value > 0.0) || (item->value < 0.0)) {
								const double f = frexp(item->value, &e);

								mantissa *= f;
								exponent += e;
								n += 1.0;
							}
						}
						if (n > 0.0) {
							const double inverse_n = 1.0 / (double)n;

							geometric_mean = pow(mantissa, inverse_n) * pow(2.0, (double)exponent * inverse_n);
						} else {
							geometric_mean = 0.0;
						}
						if (g_opt_flags & OPT_FLAGS_SN) {
							pr_metrics("%-13s %13.2e %s (geometric mean of %" PRIu32 " instance%s)\n",
								name, geometric_mean, description,
								ss->completed_instances, plural);
						} else {
							pr_metrics("%-13s %13.2f %s (geometric mean of %" PRIu32 " instance%s)\n",
								name, geometric_mean, description,
								ss->completed_instances, plural);
						}
						break;
					case STRESS_METRIC_HARMONIC_MEAN:
						sum = 0.0;
						n = 0.0;

						for (j = 0; j < ss->instances; j++) {
							const stress_stats_t *const stats = ss->stats[j];

							item = &stats->metrics.items[i];
							if ((item->value > 0.0) || (item->value < 0.0)) {
								const double reciprocal = 1.0 / item->value;

								sum += reciprocal;
								n += 1.0;
							}
						}
						if (sum > 0.0) {
							harmonic_mean = n / sum;
						} else {
							harmonic_mean = 0.0;
						}
						if (g_opt_flags & OPT_FLAGS_SN) {
							pr_metrics("%-13s %13.2e %s (harmonic mean of %" PRIu32 " instance%s)\n",
								name, harmonic_mean, description,
								ss->completed_instances, plural);
						} else {
							pr_metrics("%-13s %13.2f %s (harmonic mean of %" PRIu32 " instance%s)\n",
								name, harmonic_mean, description,
								ss->completed_instances, plural);
						}
						break;
					case STRESS_METRIC_TOTAL:
						for (j = 0; j < ss->instances; j++) {
							const stress_stats_t *const stats = ss->stats[j];

							item = &stats->metrics.items[i];
							if (item->value > 0.0)
								total += item->value;
						}
						if (g_opt_flags & OPT_FLAGS_SN) {
							pr_metrics("%-13s %13.2e %s (total of %" PRIu32 " instance%s)\n",
								name, total, description,
								ss->completed_instances, plural);
						} else {
							pr_metrics("%-13s %13.2f %s (total of %" PRIu32 " instance%s)\n",
								name, total, description,
								ss->completed_instances, plural);
						}
						break;
					case STRESS_METRIC_MAXIMUM:
						for (j = 0; j < ss->instances; j++) {
							const stress_stats_t *const stats = ss->stats[j];

							item = &stats->metrics.items[i];
							if ((item->value > 0.0) && (item->value > maximum))
								maximum = item->value;
						}
						if (g_opt_flags & OPT_FLAGS_SN) {
							pr_metrics("%-13s %13.2e %s (maximum of %" PRIu32 " instance%s)\n",
								name, maximum, description,
								ss->completed_instances, plural);
						} else {
							pr_metrics("%-13s %13.2f %s (maximum of %" PRIu32 " instance%s)\n",
								name, maximum, description,
								ss->completed_instances, plural);
						}
						break;
					}
				}
			}
		}
	}
	pr_block_end();
}

/*
 *  stress_times_dump()
 *	output the run times
 */
static void stress_times_dump(
	FILE *yaml,
	const int32_t ticks_per_sec,
	const double duration)
{
	struct tms buf;
	double total_cpu_time = stress_get_processors_configured() * duration;
	double u_time, s_time, t_time, u_pc, s_pc, t_pc;
	double min1, min5, min15;
	int rc;

	if (!(g_opt_flags & OPT_FLAGS_TIMES))
		return;

	if (times(&buf) == (clock_t)-1) {
		pr_err("cannot get run time information, errno=%d (%s)\n",
			errno, strerror(errno));
		return;
	}
	rc = stress_get_load_avg(&min1, &min5, &min15);

	u_time = (double)buf.tms_cutime / (double)ticks_per_sec;
	s_time = (double)buf.tms_cstime / (double)ticks_per_sec;
	t_time = ((double)buf.tms_cutime + (double)buf.tms_cstime) / (double)ticks_per_sec;
	u_pc = (total_cpu_time > 0.0) ? 100.0 * u_time / total_cpu_time : 0.0;
	s_pc = (total_cpu_time > 0.0) ? 100.0 * s_time / total_cpu_time : 0.0;
	t_pc = (total_cpu_time > 0.0) ? 100.0 * t_time / total_cpu_time : 0.0;

	pr_inf("for a %.2fs run time:\n", duration);
	pr_inf("  %8.2fs available CPU time\n",
		total_cpu_time);
	pr_inf("  %8.2fs user time   (%6.2f%%)\n", u_time, u_pc);
	pr_inf("  %8.2fs system time (%6.2f%%)\n", s_time, s_pc);
	pr_inf("  %8.2fs total time  (%6.2f%%)\n", t_time, t_pc);
	if (!rc) {
		pr_inf("load average: %.2f %.2f %.2f\n",
			min1, min5, min15);
	}

	pr_yaml(yaml, "times:\n");
	pr_yaml(yaml, "      run-time: %f\n", duration);
	pr_yaml(yaml, "      available-cpu-time: %f\n", total_cpu_time);
	pr_yaml(yaml, "      user-time: %f\n", u_time);
	pr_yaml(yaml, "      system-time: %f\n", s_time);
	pr_yaml(yaml, "      total-time: %f\n", t_time);
	pr_yaml(yaml, "      user-time-percent: %f\n", u_pc);
	pr_yaml(yaml, "      system-time-percent: %f\n", s_pc);
	pr_yaml(yaml, "      total-time-percent: %f\n", t_pc);
	if (!rc) {
		pr_yaml(yaml, "      load-average-1-minute: %f\n", min1);
		pr_yaml(yaml, "      load-average-5-minute: %f\n", min5);
		pr_yaml(yaml, "      load-average-15-minute: %f\n", min15);
	}
}

/*
 *  stress_log_args()
 *	dump to syslog argv[]
 */
static void stress_log_args(int argc, char **argv)
{
	size_t i, len, buflen, *arglen;
	char *buf;
	const char *user = shim_getlogin();
	const uid_t uid = getuid();

	arglen = (size_t *)calloc((size_t)argc, sizeof(*arglen));
	if (!arglen)
		return;

	for (buflen = 0, i = 0; i < (size_t)argc; i++) {
		arglen[i] = strlen(argv[i]);
		buflen += arglen[i] + 1;
	}

	buf = (char *)calloc(buflen, sizeof(*buf));
	if (!buf) {
		free(arglen);
		return;
	}

	for (len = 0, i = 0; i < (size_t)argc; i++) {
		if (i) {
			(void)shim_strlcat(buf + len, " ", buflen - len);
			len++;
		}
		(void)shim_strlcat(buf + len, argv[i], buflen - len);
		len += arglen[i];
	}
	if (user) {
		shim_syslog(LOG_INFO, "invoked with '%s' by user %" PRIdMAX " '%s'\n", buf, (intmax_t)uid, user);
		pr_dbg("invoked with '%s' by user %" PRIdMAX " '%s'\n", buf, (intmax_t)uid, user);
	} else {
		shim_syslog(LOG_INFO, "invoked with '%s' by user %" PRIdMAX "\n", buf, (intmax_t)uid);
		pr_dbg("invoked with '%s' by user % "PRIdMAX "\n", buf, (intmax_t)uid);
	}
	free(buf);
	free(arglen);
}

/*
 *  stress_log_system_mem_info()
 *	dump system memory info
 */
void stress_log_system_mem_info(void)
{
#if defined(HAVE_SYS_SYSINFO_H) && \
    defined(HAVE_SYSINFO) && \
    defined(HAVE_SYSLOG_H)
	struct sysinfo info;

	(void)shim_memset(&info, 0, sizeof(info));
	if (sysinfo(&info) == 0) {
		shim_syslog(LOG_INFO, "memory (MB): total %.2f, "
			"free %.2f, "
			"shared %.2f, "
			"buffer %.2f, "
			"swap %.2f, "
			"free swap %.2f\n",
			(double)(info.totalram * info.mem_unit) / MB,
			(double)(info.freeram * info.mem_unit) / MB,
			(double)(info.sharedram * info.mem_unit) / MB,
			(double)(info.bufferram * info.mem_unit) / MB,
			(double)(info.totalswap * info.mem_unit) / MB,
			(double)(info.freeswap * info.mem_unit) / MB);
	}
#endif
}

/*
 *  stress_log_system_info()
 *	dump system info
 */
static void stress_log_system_info(void)
{
#if defined(HAVE_UNAME) && 		\
    defined(HAVE_SYS_UTSNAME_H) &&	\
    defined(HAVE_SYSLOG_H)
	struct utsname buf;

	if (uname(&buf) >= 0) {
		shim_syslog(LOG_INFO, "system: '%s' %s %s %s %s\n",
			buf.nodename,
			buf.sysname,
			buf.release,
			buf.version,
			buf.machine);
	}
#endif
}

static void *stress_map_page(int prot, char *prot_str, size_t page_size)
{
	void *ptr;

	ptr = stress_mmap_anon_shared(page_size, prot);
	if (ptr == MAP_FAILED) {
		pr_err("cannot mmap %s %zu byte shared page%s, errno=%d (%s)\n",
			prot_str, page_size, stress_get_memfree_str(),
			errno, strerror(errno));
	}
	return ptr;
}

/*
 *  stress_shared_map()
 *	mmap shared region, with an extra page at the end
 *	that is marked read-only to stop accidental smashing
 *	from a run-away stack expansion
 */
static inline void stress_shared_map(const int32_t num_procs)
{
	const size_t page_size = stress_get_page_size();
	size_t len = sizeof(stress_shared_t) +
		     (sizeof(stress_stats_t) * (size_t)num_procs);
	size_t sz = (len + (page_size << 1)) & ~(page_size - 1);
#if defined(HAVE_MPROTECT)
	void *last_page;
#endif

	g_shared = (stress_shared_t *)stress_mmap_anon_shared(sz,  PROT_READ | PROT_WRITE);
	if (g_shared == MAP_FAILED) {
		pr_err("cannot mmap %zu byte shared memory region%s, errno=%d (%s)\n",
			sz, stress_get_memfree_str(), errno, strerror(errno));
		stress_stressors_free();
		exit(EXIT_FAILURE);
	}
	stress_set_vma_anon_name(g_shared, sz, "g_shared");

	/* Paraniod */
	(void)shim_memset(g_shared, 0, sz);
	g_shared->length = sz;
	g_shared->instance_count.started = 0;
	g_shared->instance_count.exited = 0;
	g_shared->instance_count.reaped = 0;
	g_shared->instance_count.failed = 0;
	g_shared->instance_count.alarmed = 0;
	g_shared->time_started = stress_time_now();

	/*
	 * libc on some systems warn that vfork is deprecated,
	 * we know this, force warnings off where possible
	 */
STRESS_PRAGMA_PUSH
STRESS_PRAGMA_WARN_OFF
#if defined(HAVE_VFORK)
	g_shared->vfork = vfork;
#else
	g_shared->vfork = fork;
#endif
STRESS_PRAGMA_POP

#if defined(HAVE_MPROTECT)
	last_page = ((uint8_t *)g_shared) + sz - page_size;

	/* Make last page trigger a segfault if it is accessed */
	(void)mprotect(last_page, page_size, PROT_NONE);
	stress_set_vma_anon_name(last_page, page_size,  "g_shared_guard");
#elif defined(HAVE_MREMAP) &&	\
      defined(MAP_FIXED)
	{
		void *new_last_page;

		/* Try to remap last page with PROT_NONE */
		(void)munmap(last_page, page_size);
		new_last_page = mmap(last_page, page_size, PROT_NONE,
			MAP_SHARED | MAP_ANON | MAP_FIXED, -1, 0);

		/* Failed, retry read-only */
		if (new_last_page == MAP_FAILED)
			new_last_page = mmap(last_page, page_size, PROT_READ,
				MAP_SHARED | MAP_ANON | MAP_FIXED, -1, 0);
		/* Can't remap, bump length down a page */
		if (new_last_page == MAP_FAILED)
			g_shared->length -= sz;
		if (new_last_page != MAP_FAILED)
			stress_set_vma_anon_name(last_page, page_size,  "g_shared_guard");
	}
#endif

	/*
	 *  copy of checksums and run data in a different shared
	 *  memory segment so that we can sanity check these for
	 *  any form of corruption
	 */
	len = sizeof(stress_checksum_t) * (size_t)num_procs;
	sz = (len + page_size) & ~(page_size - 1);
	g_shared->checksum.checksums = (stress_checksum_t *)stress_mmap_anon_shared(sz, PROT_READ | PROT_WRITE);
	if (g_shared->checksum.checksums == MAP_FAILED) {
		pr_err("cannot mmap %zu byte checksums%s, errno=%d (%s)\n",
			sz, stress_get_memfree_str(),
			errno, strerror(errno));
		goto err_unmap_shared;
	}
	stress_set_vma_anon_name(g_shared->checksum.checksums, len, "checksum");
	(void)shim_memset(g_shared->checksum.checksums, 0, sz);
	g_shared->checksum.length = sz;

	/*
	 *  mmap some pages for testing invalid arguments in
	 *  various stressors, get the allocations done early
	 *  to avoid later mmap failures on stressor child
	 *  processes
	 */
	g_shared->mapped.page_none = stress_map_page(PROT_NONE, "PROT_NONE", page_size);
	if (g_shared->mapped.page_none == MAP_FAILED)
		goto err_unmap_checksums;
	stress_set_vma_anon_name(g_shared->mapped.page_none, page_size, "mapped-none");

	g_shared->mapped.page_ro = stress_map_page(PROT_READ, "PROT_READ", page_size);
	if (g_shared->mapped.page_ro == MAP_FAILED)
		goto err_unmap_page_none;
	stress_set_vma_anon_name(g_shared->mapped.page_ro, page_size, "mapped-ro");

	g_shared->mapped.page_wo = stress_map_page(PROT_WRITE, "PROT_WRITE", page_size);
	if (g_shared->mapped.page_wo == MAP_FAILED)
		goto err_unmap_page_ro;
	stress_set_vma_anon_name(g_shared->mapped.page_ro, page_size, "mapped-wo");

	return;

err_unmap_page_ro:
	(void)stress_munmap_anon_shared((void *)g_shared->mapped.page_ro, page_size);
err_unmap_page_none:
	(void)stress_munmap_anon_shared((void *)g_shared->mapped.page_none, page_size);
err_unmap_checksums:
	(void)stress_munmap_anon_shared((void *)g_shared->checksum.checksums, g_shared->checksum.length);
err_unmap_shared:
	(void)stress_munmap_anon_shared((void *)g_shared, g_shared->length);
	stress_stressors_free();
	exit(EXIT_FAILURE);
}

/*
 *  stress_shared_readonly()
 *	unmap shared region
 */
void stress_shared_readonly(void)
{
#if defined(HAVE_MPROTECT)
	(void)mprotect((void *)g_shared->checksum.checksums, g_shared->checksum.length, PROT_READ);
	(void)mprotect((void *)g_shared, g_shared->length, PROT_READ);
#endif
}

/*
 *  stress_shared_unmap()
 *	unmap shared region
 */
void stress_shared_unmap(void)
{
	const size_t page_size = stress_get_page_size();

	(void)stress_munmap_anon_shared((void *)g_shared->mapped.page_wo, page_size);
	(void)stress_munmap_anon_shared((void *)g_shared->mapped.page_ro, page_size);
	(void)stress_munmap_anon_shared((void *)g_shared->mapped.page_none, page_size);
	(void)stress_munmap_anon_shared((void *)g_shared->checksum.checksums, g_shared->checksum.length);
	(void)stress_munmap_anon_shared((void *)g_shared, g_shared->length);
}

/*
 *  stress_exclude_unimplemented()
 *	report why an unimplemented stressor will be skipped
 */
static void stress_exclude_unimplemented(
	const char *name,
	const stressor_info_t *info)
{
	static const char msg[] = "stressor will be skipped, it is not implemented on "
				  "this system";
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	struct utsname buf;

	if (uname(&buf) >= 0) {
		if (info->unimplemented_reason) {
			pr_inf_skip("%s %s: %s %s (%s)\n",
				name, msg, stress_get_uname_info(),
				stress_get_compiler(),
				info->unimplemented_reason);
		} else {
			pr_inf_skip("%s %s: %s %s\n",
				name, msg, stress_get_uname_info(),
				stress_get_compiler());
		}
	}
#else
	if (info->unimplemented_reason) {
		pr_inf_skip("%s %s: %s (%s)\n",
			name, msg, stress_get_compiler(),
			info->unimplemented_reason);
	} else {
		pr_inf_skip("%s %s: %s\n",
			name, msg, stress_get_compiler());
	}
#endif
}

/*
 *  stress_exclude_unsupported()
 *	tag stressor proc count to be excluded
 */
static inline void stress_exclude_unsupported(bool *unsupported)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
		const stress_t *stressor = &stressors[i];

		if (!stressor->info)
			continue;

		if (stressor->info->supported) {
			stress_stressor_t *ss;

			for (ss = stress_stressor_list.head; ss; ss = ss->next) {
				if (ss->ignore.run)
					continue;

				if ((ss->stressor == stressor) && ss->instances &&
					(stressor->info->supported(ss->stressor->name) < 0)) {
						stress_ignore_stressor(ss, STRESS_STRESSOR_UNSUPPORTED);
						*unsupported = true;
				}
			}
		}
		if (stressor->info->stressor == stress_unimplemented) {
			stress_stressor_t *ss;

			for (ss = stress_stressor_list.head; ss; ss = ss->next) {
				if (ss->ignore.run)
					continue;

				if ((ss->stressor == stressor) && ss->instances) {
					stress_exclude_unimplemented(ss->stressor->name, stressor->info);
					stress_ignore_stressor(ss, STRESS_STRESSOR_UNSUPPORTED);
					*unsupported = true;
				}
			}
		}
	}
}

/*
 *  stress_set_proc_limits()
 *	set maximum number of processes for specific stressors
 */
static void stress_set_proc_limits(void)
{
#if defined(RLIMIT_NPROC)
	stress_stressor_t *ss;
	struct rlimit limit;

	if (getrlimit(RLIMIT_NPROC, &limit) < 0)
		return;

	for (ss = stress_stressor_list.head; ss; ss = ss->next) {
		const stressor_info_t *info;

		if (ss->ignore.run)
			continue;

		info = ss->stressor->info;
		if (info && info->set_limit && ss->instances) {
			const uint64_t max = (uint64_t)limit.rlim_cur / (uint64_t)ss->instances;
			info->set_limit(max);
		}
	}
#endif
}

static inline void stress_append_stressor(stress_stressor_t *ss)
{
	ss->prev = NULL;
	ss->next = NULL;

	/* Add to end of procs list */
	if (stress_stressor_list.tail)
		stress_stressor_list.tail->next = ss;
	else
		stress_stressor_list.head = ss;
	ss->prev = stress_stressor_list.tail;
	stress_stressor_list.tail = ss;
}

/*
 *  stress_find_proc_info()
 *	find proc info that is associated with a specific
 *	stressor.  If it does not exist, create a new one
 *	and return that. Terminate if out of memory.
 */
static stress_stressor_t *stress_find_proc_info(const stress_t *stressor)
{
	stress_stressor_t *ss;

#if 0
	/* Scan backwards in time to find last matching stressor */
	for (ss = stressors_tail; ss; ss = ss->prev) {
		if (ss->stressor == stressor)
			return ss;
	}
#endif
	ss = (stress_stressor_t *)calloc(1, sizeof(*ss));
	if (!ss) {
		(void)fprintf(stderr, "cannot allocate %zu byte stressor state info%s\n",
			sizeof(*ss), stress_get_memfree_str());
		exit(EXIT_FAILURE);
	}
	ss->stressor = stressor;
	ss->ignore.run = STRESS_STRESSOR_NOT_IGNORED;
	stress_append_stressor(ss);

	return ss;
}

/*
 *  stress_stressors_init()
 *	initialize any stressors that will be used
 */
static void stress_stressors_init(void)
{
	stress_stressor_t *ss;

	for (ss = stress_stressor_list.head; ss; ss = ss->next) {
		const stressor_info_t *info;

		if (ss->ignore.run)
			continue;

		info = ss->stressor->info;
		if (info && info->init)
			info->init(ss->instances);
	}
}

/*
 *  stress_stressors_deinit()
 *	de-initialize any stressors that will be used
 */
static void stress_stressors_deinit(void)
{
	stress_stressor_t *ss;

	for (ss = stress_stressor_list.head; ss; ss = ss->next) {
		const stressor_info_t *info;

		if (ss->ignore.run)
			continue;

		info = ss->stressor->info;
		if (info && info->deinit)
			info->deinit();
	}
}

/*
 *  stressor_set_defaults()
 *	set up stressor default settings that can be overridden
 *	by user later on
 */
static inline void stressor_set_defaults(void)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
		if (stressors[i].info && stressors[i].info->set_default)
			stressors[i].info->set_default();
	}
}

/*
 *  stress_exclude_pathological()
 *	Disable pathological stressors if user has not explicitly
 *	request them to be used. Let's play safe.
 */
static inline void stress_exclude_pathological(void)
{
	if (!(g_opt_flags & OPT_FLAGS_PATHOLOGICAL)) {
		stress_stressor_t *ss = stress_stressor_list.head;

		while (ss) {
			stress_stressor_t *next = ss->next;

			if ((!ss->ignore.run) && (ss->stressor->info->classifier & CLASS_PATHOLOGICAL)) {
				if (ss->instances > 0) {
					const char* name = ss->stressor->name;

					pr_inf("disabled '%s' as it "
						"may hang or reboot the machine "
						"(enable it with the "
						"--pathological option)\n", name);
				}
				stress_ignore_stressor(ss, STRESS_STRESSOR_EXCLUDED);
			}
			ss = next;
		}
	}
}

/*
 *  stress_setup_stats_buffers()
 *	setup the stats data from the shared memory
 */
static inline void stress_setup_stats_buffers(void)
{
	stress_stressor_t *ss;
	stress_stats_t *stats = g_shared->stats;

	for (ss = stress_stressor_list.head; ss; ss = ss->next) {
		int32_t i;

		if (ss->ignore.run)
			continue;

		for (i = 0; i < ss->instances; i++, stats++) {
			size_t j;

			ss->stats[i] = stats;
			for (j = 0; j < SIZEOF_ARRAY(stats->metrics.items); j++) {
				stats->metrics.items[j].value = 0.0;
				stats->metrics.items[j].description = NULL;
			}
		}
	}
}

/*
 *  stress_set_random_stressors()
 *	select stressors at random
 */
static inline void stress_set_random_stressors(void)
{
	int32_t opt_random = 0;

	(void)stress_get_setting("random", &opt_random);

	if (g_opt_flags & OPT_FLAGS_RANDOM) {
		int32_t n = opt_random;
		const uint32_t n_procs = stress_get_num_stressors();

		if (g_opt_flags & OPT_FLAGS_SET) {
			(void)fprintf(stderr, "cannot specify random "
				"option with other stress processes "
				"selected\n");
			exit(EXIT_FAILURE);
		}

		if (!n_procs) {
			(void)fprintf(stderr,
				"No stressors are available, unable to continue\n");
			exit(EXIT_FAILURE);
		}

		/* create n randomly chosen stressors */
		while (n > 0) {
			const uint32_t i = stress_mwc32modn(n_procs);
			stress_stressor_t *ss = stress_get_nth_stressor(i);

			if (!ss)
				continue;

			ss->instances++;
			n--;
		}
	}
}

static void stress_with(const int32_t instances)
{
	char *opt_with = NULL, *str, *token;

	(void)stress_get_setting("with", &opt_with);

	for (str = opt_with; (token = strtok(str, ",")) != NULL; str = NULL) {
		stress_stressor_t *ss;
		ssize_t i = stress_stressor_find(token);

		if (i < 0) {
			(void)fprintf(stderr, "Unknown stressor: '%s', "
				"invalid --with option\n", token);
			exit(EXIT_FAILURE);
		}
		ss = stress_find_proc_info(&stressors[i]);
		if (!ss) {
			(void)fprintf(stderr, "cannot %zu byte allocate stressor state info%s\n",
				sizeof(*ss), stress_get_memfree_str());
			exit(EXIT_FAILURE);
		}
		ss->instances = instances;
	}
	return;
}

/*
 *  stress_enable_all_stressors()
 *	enable all the stressors
 */
static void stress_enable_all_stressors(const int32_t instances)
{
	size_t i;

	if (g_opt_flags & OPT_FLAGS_WITH) {
		stress_with(instances);
		return;
	}

	/* Don't enable all if some stressors are set */
	if (g_opt_flags & OPT_FLAGS_SET)
		return;

	for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
		stress_stressor_t *ss = stress_find_proc_info(&stressors[i]);

		if (!ss) {
			(void)fprintf(stderr, "cannot %zu byte allocate stressor state info%s\n",
				sizeof(*ss), stress_get_memfree_str());
			exit(EXIT_FAILURE);
		}
		ss->instances = instances;
	}
}

/*
 *  stress_enable_classes()
 *	enable stressors based on class
 */
static void stress_enable_classes(const uint32_t classifier)
{
	size_t i;

	if (!classifier)
		return;

	/* This indicates some stressors are set */
	g_opt_flags |= OPT_FLAGS_SET;

	for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
		if (stressors[i].info->classifier & classifier) {
			stress_stressor_t *ss = stress_find_proc_info(&stressors[i]);

			if (g_opt_flags & OPT_FLAGS_SEQUENTIAL)
				ss->instances = g_opt_sequential;
			else if (g_opt_flags & OPT_FLAGS_ALL)
				ss->instances = g_opt_parallel;
			else if (g_opt_flags & OPT_FLAGS_PERMUTE)
				ss->instances = g_opt_permute;
		}
	}
}

/*
 *  stress_parse_limit()
 *	parse rlimit resource values
 */
static void stress_parse_limit(const char *opt, const char *option)
{
	const size_t page_size = stress_get_page_size();
	uint64_t u64 = stress_get_uint64_byte(opt);

	/* round down to page boundary */
	u64 &= ~(uint64_t)(page_size - 1);
	if (sizeof(rlim_t) <= 4) {
		stress_check_range_bytes(option, u64, 1 * MB, (uint64_t)~(uint32_t)(page_size - 1));
	} else {
		stress_check_range_bytes(option, u64, 1 * MB, ~(uint64_t)(page_size - 1));
	}
	stress_set_setting_global(option, TYPE_ID_UINT64, &u64);
}

/*
 *  stress_parse_opts
 *	parse argv[] and set stress-ng options accordingly
 */
int stress_parse_opts(int argc, char **argv, const bool jobmode)
{
	optind = 0;

	for (;;) {
		int64_t i64;
		int32_t i32;
		uint32_t u32;
		uint64_t u64, max_fds;
		int16_t i16;
		int c, option_index, ret;
		size_t i;

		opterr = (!jobmode) ? opterr : 0;
next_opt:
		if ((c = getopt_long(argc, argv, "?AB:C:D:F:I:KL:MOP:R:S:T:VY:a:b:c:d:f:hi:j:kl:m:no:p:vqr:s:t:u:w:x:y:",
			stress_long_options, &option_index)) == -1) {
			break;
		}

		for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
			if (stressors[i].short_getopt == c) {
				const char *name = stress_opt_name(c);
				stress_stressor_t *ss = stress_find_proc_info(&stressors[i]);

				g_stressor_current = ss;
				g_opt_flags |= OPT_FLAGS_SET;
				ss->instances = stress_get_int32_instance_percent(optarg);
				stress_get_processors(&ss->instances);
				stress_check_max_stressors(name, ss->instances);
				goto next_opt;
			}
			if (stressors[i].op == (stress_op_t)c) {
				uint64_t bogo_max_ops;

				bogo_max_ops = stress_get_uint64(optarg);
				stress_check_range(stress_opt_name(c), bogo_max_ops, MIN_OPS, MAX_OPS);
				/* We don't need to set this, but it may be useful */
				stress_set_setting_global(stress_opt_name(c), TYPE_ID_UINT64, &bogo_max_ops);
				if (g_stressor_current)
					g_stressor_current->bogo_max_ops = bogo_max_ops;
				goto next_opt;
			}
			if (stressors[i].info->opts) {
				size_t j;
				const stressor_info_t *info = stressors[i].info;

				for (j = 0; info->opts[j].opt_name; j++) {
					if (info->opts[j].opt == c) {
						ret = stress_parse_opt(stressors[i].name, optarg, &info->opts[j]);
						if (ret < 0)
							return EXIT_FAILURE;
						goto next_opt;
					}
				}
			}
		}

		for (i = 0; i < SIZEOF_ARRAY(opt_flags); i++) {
			if (c == opt_flags[i].opt) {
				stress_set_setting_true("global", stress_opt_name(c), NULL);
				g_opt_flags |= opt_flags[i].opt_flag;
				goto next_opt;
			}
		}

		switch (c) {
		case OPT_all:
			g_opt_flags |= OPT_FLAGS_ALL;
			g_opt_parallel= stress_get_int32_instance_percent(optarg);
			stress_get_processors(&g_opt_parallel);
			stress_check_max_stressors("all", g_opt_parallel);
			break;
		case OPT_cache_size:
			/* 1K..4GB should be enough range  */
			u64 = stress_get_uint64_byte(optarg);
			stress_check_range_bytes("cache-size", u64, 1 * KB, 4 * GB);
			/* round down to 64 byte boundary */
			u64 &= ~(uint64_t)63;
			stress_set_setting_global("cache-size", TYPE_ID_UINT64, &u64);
			break;
		case OPT_backoff:
			i64 = (int64_t)stress_get_uint64(optarg);
			stress_set_setting_global("backoff", TYPE_ID_INT64, &i64);
			break;
		case OPT_cache_level:
			/*
			 * Note: Overly high values will be caught in the
			 * caching code.
			 */
			ret = atoi(optarg);
			if ((ret <= 0) || (ret > 3))
				ret = DEFAULT_CACHE_LEVEL;
			i16 = (int16_t)ret;
			stress_set_setting_global("cache-level", TYPE_ID_INT16, &i16);
			break;
		case OPT_cache_ways:
			u32 = stress_get_uint32(optarg);
			stress_set_setting_global("cache-ways", TYPE_ID_UINT32, &u32);
			break;
		case OPT_class:
			ret = stress_get_class(optarg, &u32);
			if (ret < 0)
				return EXIT_FAILURE;
			else if (ret > 0)
				exit(EXIT_SUCCESS);
			else {
				stress_set_setting_global("class", TYPE_ID_UINT32, &u32);
				stress_enable_classes(u32);
			}
			break;
		case OPT_config:
			printf("config:\n%s", stress_config);
			exit(EXIT_SUCCESS);
		case OPT_exclude:
			stress_set_setting_global("exclude", TYPE_ID_STR, (void *)optarg);
			break;
		case OPT_help:
			stress_usage();
			break;
		case OPT_ionice_class:
			i32 = stress_get_opt_ionice_class(optarg);
			stress_set_setting_global("ionice-class", TYPE_ID_INT32, &i32);
			break;
		case OPT_ionice_level:
			i32 = stress_get_int32(optarg);
			stress_set_setting_global("ionice-level", TYPE_ID_INT32, &i32);
			break;
		case OPT_job:
			stress_set_setting_global("job", TYPE_ID_STR, (void *)optarg);
			break;
		case OPT_limit_as:
			stress_parse_limit(optarg, "limit-as");
			break;
		case OPT_limit_data:
			stress_parse_limit(optarg, "limit-data");
			break;
		case OPT_limit_stack:
			stress_parse_limit(optarg, "limit-stack");
			break;
		case OPT_log_file:
			stress_set_setting_global("log-file", TYPE_ID_STR, (void *)optarg);
			break;
		case OPT_max_fd:
			max_fds = (uint64_t)stress_get_file_limit();
			u64 = stress_get_uint64_percent(optarg, 1, max_fds, NULL,
				"cannot determine maximum file descriptor limit");
			stress_check_range(optarg, u64, 8, max_fds);
			stress_set_setting_global("max-fd", TYPE_ID_UINT64, &u64);
			break;
		case OPT_mbind:
			if (stress_set_mbind(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_no_madvise:
			g_opt_flags &= ~OPT_FLAGS_MMAP_MADVISE;
			break;
		case OPT_oom_avoid_bytes:
			{
				size_t shmall, freemem, totalmem, freeswap, totalswap, bytes;

				bytes = (size_t)stress_get_uint64_byte_memory(optarg, 1);
				stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);
				if ((freemem > 0) && (bytes > freemem / 2)) {
					char buf[32];

					bytes = freemem / 2;
					pr_inf("option --oom-avoid-bytes too large, limiting to "
						"50%% (%s) of free memory\n",
						stress_uint64_to_str(buf, sizeof(buf), (uint64_t)bytes, 1, true));
				}
				stress_set_setting_global("oom-avoid-bytes", TYPE_ID_SIZE_T, &bytes);
				g_opt_flags |= OPT_FLAGS_OOM_AVOID;
			}
			break;
		case OPT_pause:
			g_opt_pause = stress_get_uint(optarg);
			stress_set_setting_global("pause", TYPE_ID_UINT, &g_opt_pause);
			break;
		case OPT_query:
			if (!jobmode)
				(void)printf("Try '%s --help' for more information.\n", g_app_name);
			return EXIT_FAILURE;
		case OPT_quiet:
			g_opt_flags &= ~(OPT_FLAGS_PR_ALL);
			break;
		case OPT_random:
			g_opt_flags |= OPT_FLAGS_RANDOM;
			i32 = stress_get_int32(optarg);
			stress_get_processors(&i32);
			stress_check_max_stressors("random", i32);
			stress_set_setting_global("random", TYPE_ID_INT32, &i32);
			break;
		case OPT_resctrl:
			if (stress_resctrl_parse(optarg) < 0)
				return EXIT_FAILURE;
			stress_set_setting_global("resctrl", TYPE_ID_STR, optarg);
			break;
		case OPT_sched:
			i32 = stress_get_opt_sched(optarg);
			stress_set_setting_global("sched", TYPE_ID_INT32, &i32);
			break;
		case OPT_sched_prio:
			i32 = stress_get_int32(optarg);
			stress_set_setting_global("sched-prio", TYPE_ID_INT32, &i32);
			break;
		case OPT_sched_period:
			u64 = stress_get_uint64(optarg);
			stress_set_setting_global("sched-period", TYPE_ID_UINT64, &u64);
			break;
		case OPT_sched_runtime:
			u64 = stress_get_uint64(optarg);
			stress_set_setting_global("sched-runtime", TYPE_ID_UINT64, &u64);
			break;
		case OPT_sched_deadline:
			u64 = stress_get_uint64(optarg);
			stress_set_setting_global("sched-deadline", TYPE_ID_UINT64, &u64);
			break;
		case OPT_sched_reclaim:
			g_opt_flags |= OPT_FLAGS_DEADLINE_GRUB;
			break;
		case OPT_seed:
			u64 = stress_get_uint64(optarg);
			g_opt_flags |= OPT_FLAGS_SEED;
			stress_set_setting_global("seed", TYPE_ID_UINT64, &u64);
			break;
		case OPT_sequential:
			g_opt_flags |= OPT_FLAGS_SEQUENTIAL;
			g_opt_sequential = stress_get_int32_instance_percent(optarg);
			stress_get_processors(&g_opt_sequential);
			stress_check_range("sequential", (uint64_t)g_opt_sequential,
				MIN_SEQUENTIAL, MAX_SEQUENTIAL);
			break;
		case OPT_permute:
			g_opt_flags |= OPT_FLAGS_PERMUTE;
			g_opt_permute = stress_get_int32_instance_percent(optarg);
			stress_get_processors(&g_opt_permute);
			stress_check_max_stressors("permute", g_opt_permute);
			break;
		case OPT_status:
			if (stress_set_status(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_stressors:
			stress_show_stressor_names();
			exit(EXIT_SUCCESS);
		case OPT_taskset:
			if (stress_set_cpu_affinity(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_temp_path:
			stress_set_setting_global("temp-path", TYPE_ID_STR, optarg);
			break;
		case OPT_timeout:
			g_opt_timeout = stress_get_uint64_time(optarg);
			break;
		case OPT_timer_slack:
			(void)stress_set_timer_slack_ns(optarg);
			break;
		case OPT_version:
			stress_version();
			exit(EXIT_SUCCESS);
		case OPT_verifiable:
			stress_verifiable();
			exit(EXIT_SUCCESS);
		case OPT_vmstat:
			if (stress_set_vmstat(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_vmstat_units:
			stress_set_vmstat_units(optarg);
			break;
		case OPT_thermalstat:
			if (stress_set_thermalstat(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_iostat:
			if (stress_set_iostat(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_raplstat:
			if (stress_set_raplstat(optarg) < 0)
				exit(EXIT_FAILURE);
			g_opt_flags |= OPT_FLAGS_RAPL_REQUIRED;
			break;
		case OPT_with:
			g_opt_flags |= (OPT_FLAGS_WITH | OPT_FLAGS_SET);
			stress_set_setting_global("with", TYPE_ID_STR, (void *)optarg);
			break;
		case OPT_yaml:
			stress_set_setting_global("yaml", TYPE_ID_STR, (void *)optarg);
			break;
		default:
			if (!jobmode)
				(void)printf("Unknown option (%d)\n",c);
			return EXIT_FAILURE;
		}
	}

	if (optind < argc) {
		bool unicode = false;

		(void)printf("Error: unrecognised option:");
		while (optind < argc) {
			(void)printf(" %s", argv[optind]);
			if (((argv[optind][0] & 0xff) == 0xe2) &&
			    ((argv[optind][1] & 0xff) == 0x88)) {
				unicode = true;
			}
			optind++;
		}
		(void)printf("\n");
		if (unicode)
			(void)printf("note: a Unicode minus sign was used instead of an ASCII '-' for an option\n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_alloc_proc_stas()
 *	allocate array of stressor stats based on n stats required
 */
static void stress_alloc_proc_stats(
	stress_stats_t ***stats,
	const int32_t n)
{
	*stats = (stress_stats_t **)calloc((size_t)n, sizeof(stress_stats_t *));
	if (!*stats) {
		pr_err("cannot allocate stats array of %" PRIu32 " elements%s\n",
			n, stress_get_memfree_str());
		stress_stressors_free();
		exit(EXIT_FAILURE);
	}
}

/*
 *  stress_set_default_timeout()
 *	set timeout to a default value if not already set
 */
static void stress_set_default_timeout(const uint64_t timeout)
{
	char *action;

	if (g_opt_timeout == TIMEOUT_NOT_SET) {
		g_opt_timeout = timeout;
		action = "defaulting";
	} else {
		action = "setting";
	}
	pr_inf("%s to a %s run per stressor\n",
		action, (g_opt_timeout == 0) ? "infinite" :
		stress_duration_to_str((double)g_opt_timeout, false, false));
}

/*
 *  stress_setup_sequential()
 *	setup for sequential --seq mode stressors
 */
static void stress_setup_sequential(const uint32_t classifier, const int32_t instances)
{
	stress_stressor_t *ss;

	stress_set_default_timeout(60);

	for (ss = stress_stressor_list.head; ss; ss = ss->next) {
		if (ss->stressor->info->classifier & classifier)
			ss->instances = instances;
		if (!ss->ignore.run)
			stress_alloc_proc_stats(&ss->stats, ss->instances);
	}
}

/*
 *  stress_setup_parallel()
 *	setup for parallel mode stressors
 */
static void stress_setup_parallel(const uint32_t classifier, const int32_t instances)
{
	stress_stressor_t *ss;

	stress_set_default_timeout(DEFAULT_TIMEOUT);

	for (ss = stress_stressor_list.head; ss; ss = ss->next) {
		if (ss->stressor->info->classifier & classifier)
			ss->instances = instances;
		if (ss->ignore.run)
			continue;

		/*
		 * Share bogo ops between processes equally, rounding up
		 * if nonzero bogo_max_ops
		 */
		ss->bogo_max_ops = ss->instances ?
			(ss->bogo_max_ops + (ss->instances - 1)) / ss->instances : 0;
		if (ss->instances)
			stress_alloc_proc_stats(&ss->stats, ss->instances);
	}
}

/*
 *  stress_run_sequential()
 *	run stressors sequentially
 */
static inline void stress_run_sequential(
	const int32_t ticks_per_sec,
	double *duration,
	bool *success,
	bool *resource_success,
	bool *metrics_success)
{
	stress_stressor_t *ss;
	stress_checksum_t *checksum = g_shared->checksum.checksums;
	size_t total_run = 0, run = 0;
	const bool progress = !!(g_opt_flags & OPT_FLAGS_PROGRESS);

	if (progress) {
		for (ss = stress_stressor_list.head; ss; ss = ss->next) {
			if (!ss->ignore.run)
				total_run++;
		}
	}

	/*
	 *  Step through each stressor one by one
	 */
	for (ss = stress_stressor_list.head; ss && stress_continue_flag(); ss = ss->next) {
		stress_stressor_t *next;

		if (ss->ignore.run)
			continue;

		if (progress) {
			struct tm *tm_finish;
			time_t t_finish;
			const char *name = ss->stressor->name;
			char finish[64];

			t_finish = time(NULL);
			t_finish += g_opt_timeout * ((108 * (total_run - run)) / 100);
			t_finish += g_opt_pause * (total_run - run - 1);
			tm_finish = localtime(&t_finish);
			if (tm_finish)
				(void)strftime(finish, sizeof(finish), "%T %F", tm_finish);
			else
				*finish = '\0';

			run++;
			pr_inf("starting %s, %zd of %zd (%.2f%%)%s%s\n",
				name, run, total_run,
				(total_run > 0) ?  100.0 * (double)run / (double)total_run : 100.0,
				*finish ? ", finish at " : "", finish);
		}
		next = ss->next;
		ss->next = NULL;
		stress_run(ticks_per_sec, ss, duration, success, resource_success,
			metrics_success, &checksum);
		ss->next = next;
	}
	stress_metrics_check(success);
}

/*
 *  stress_run_parallel()
 *	run stressors in parallel
 */
static inline void stress_run_parallel(
	const int32_t ticks_per_sec,
	double *duration,
	bool *success,
	bool *resource_success,
	bool *metrics_success)
{
	stress_checksum_t *checksum = g_shared->checksum.checksums;

	/*
	 *  Run all stressors in parallel
	 */
	stress_run(ticks_per_sec, stress_stressor_list.head, duration, success, resource_success,
			metrics_success, &checksum);
	stress_metrics_check(success);
}

/*
 *  stress_run_permute()
 *	run stressors using permutations
 */
static inline void stress_run_permute(
	const int32_t ticks_per_sec,
	double *duration,
	bool *success,
	bool *resource_success,
	bool *metrics_success)
{
	stress_stressor_t *ss;
	size_t i, perms, num_perms, run = 0;
	const size_t max_perms = STRESS_MAX_PERMUTATIONS;
	char str[4096];

	for (perms = 0, ss = stress_stressor_list.head; ss; ss = ss->next) {
		ss->ignore.permute = true;
		if (!ss->ignore.run)
			perms++;
	}

	if (perms > max_perms) {
		pr_inf("permute: limiting to first %zu stressors\n", max_perms);
		perms = max_perms;
	}

	num_perms = (1U << perms) - 1;

	for (i = 1; stress_continue_flag() && (i <= num_perms); i++) {
		size_t j;
		struct tm *tm_finish;
		time_t t_finish;
		char finish[64];

		t_finish = time(NULL);
		t_finish += g_opt_timeout * ((108 * (num_perms - run)) / 100);
		tm_finish = localtime(&t_finish);
		if (tm_finish)
			strftime(finish, sizeof(finish), "%T %F", tm_finish);
		else
			*finish = '\0';

		*str = '\0';
		for (j = 0, ss = stress_stressor_list.head; (j < max_perms) && ss; ss = ss->next) {
			ss->ignore.permute = true;

			if (ss->ignore.run)
				continue;

			ss->ignore.permute = ((i & (1U << j)) == 0);
			if (!ss->ignore.permute) {
				if (*str)
					shim_strlcat(str, ", ", sizeof(str));
				shim_strlcat(str, ss->stressor->name, sizeof(str));
			}
			j++;
		}
		run++;
		pr_inf("starting %s, %zd of %zd (%.2f%%)%s%s\n",
			str, run, num_perms,
			(num_perms > 0) ?  100.0 * (double)run / (double)num_perms : 100.0,
			*finish ? ", finish at " : "",
			finish);
		stress_run_parallel(ticks_per_sec, duration, success, resource_success, metrics_success);
	}
	for (ss = stress_stressor_list.head; ss; ss = ss->next) {
		ss->ignore.permute = false;
	}
}

/*
 *  stress_mlock_executable()
 *	try to mlock image into memory so it
 *	won't get swapped out
 */
static inline void stress_mlock_executable(void)
{
#if defined(MLOCKED_SECTION)
	extern void *__start_mlocked_text;
	extern void *__stop_mlocked_text;

	stress_mlock_region(&__start_mlocked_text, &__stop_mlocked_text);
#endif
}

/*
 *  stress_yaml_open()
 *	open YAML results file
 */
static FILE *stress_yaml_open(const char *yaml_filename)
{
	FILE *yaml = NULL;

	if (yaml_filename) {
		yaml = fopen(yaml_filename, "w");
		if (!yaml)
			pr_err("cannot output YAML data to %s\n", yaml_filename);

		pr_yaml(yaml, "---\n");
		stress_yaml_buildinfo(yaml);
		stress_yaml_runinfo(yaml);
	}
	return yaml;
}

/*
 *  stress_yaml_close()
 *	close YAML results file
 */
static void stress_yaml_close(FILE *yaml)
{
	if (yaml) {
		pr_yaml(yaml, "...\n");
		(void)fflush(yaml);
		(void)fclose(yaml);
	}
}

/*
 *  stress_global_lock_create()
 *	create global locks
 */
static int stress_global_lock_create(void)
{
#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	g_shared->perf.lock = stress_lock_create("perf");
	if (!g_shared->perf.lock) {
		pr_err("failed to create perf lock\n");
		return -1;
	}
#endif
	g_shared->warn_once.lock = stress_lock_create("warn-once");
	if (!g_shared->warn_once.lock) {
		pr_err("failed to create warn_once lock\n");
		return -1;
	}
	g_shared->net_port_map.lock = stress_lock_create("net-port");
	if (!g_shared->net_port_map.lock) {
		pr_err("failed to create net_port_map lock\n");
		return -1;
	}
	return 0;
}

/*
 *  stress_global_lock_destroy()
 *	destroy global locks
 */
static void stress_global_lock_destroy(void)
{
	if (g_shared->net_port_map.lock)
		stress_lock_destroy(g_shared->net_port_map.lock);
	if (g_shared->warn_once.lock)
		stress_lock_destroy(g_shared->warn_once.lock);
#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	if (g_shared->perf.lock)
		stress_lock_destroy(g_shared->perf.lock);
#endif
}

static inline void stress_fixup_stressor_names(void)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stressors); i++)
		stress_munge_underscore(stressors[i].name, stressors[i].name, sizeof(stressors[i].name));
}

int main(int argc, char **argv, char **envp)
{
	double duration = 0.0;			/* stressor run time in secs */
	bool success = true;
	bool resource_success = true;
	bool metrics_success = true;
	FILE *yaml;				/* YAML output file */
	char *yaml_filename = NULL;		/* YAML file name */
	char *log_filename;			/* log filename */
	char *job_filename = NULL;		/* job filename */
	int32_t ticks_per_sec;			/* clock ticks per second (jiffies) */
	int32_t ionice_class = UNDEFINED;	/* ionice class */
	int32_t ionice_level = UNDEFINED;	/* ionice level */
	size_t i;
	uint32_t class = 0;
	const uint32_t cpus_online = (uint32_t)stress_get_processors_online();
	const uint32_t cpus_configured = (uint32_t)stress_get_processors_configured();
	int ret;
	bool unsupported = false;		/* true if stressors are unsupported */

	main_pid = getpid();

	/* Enable stress-ng stack smashing message */
	stress_set_stack_smash_check_flag(true);

	stress_fixup_stressor_names();

	stress_set_proc_name_init(argc, argv, envp);

	if (setjmp(g_error_env) == 1) {
		ret = EXIT_FAILURE;
		goto exit_ret;
	}

	yaml = NULL;

	/* --exec stressor uses this to exec itself and then exit early */
	if ((argc == 2) && !strcmp(argv[1], "--exec-exit")) {
		ret = EXIT_SUCCESS;
		goto exit_ret;
	}

	stress_stressor_list.head = NULL;
	stress_stressor_list.tail = NULL;
	stress_mwc_reseed();

	(void)stress_get_page_size();
	stressor_set_defaults();

	if (stress_get_processors_configured() < 0) {
		pr_err("sysconf failed, number of cpus configured "
			"unknown, errno=%d: (%s)\n",
			errno, strerror(errno));
		ret = EXIT_FAILURE;
		goto exit_settings_free;
	}
	ticks_per_sec = stress_get_ticks_per_second();
	if (ticks_per_sec < 0) {
		pr_err("sysconf failed, clock ticks per second "
			"unknown, errno=%d (%s)\n",
			errno, strerror(errno));
		ret = EXIT_FAILURE;
		goto exit_settings_free;
	}

	ret = stress_parse_opts(argc, argv, false);
	if (ret != EXIT_SUCCESS)
		goto exit_settings_free;

	if ((g_opt_flags & (OPT_FLAGS_STDERR | OPT_FLAGS_STDOUT)) ==
	    (OPT_FLAGS_STDERR | OPT_FLAGS_STDOUT)) {
		(void)fprintf(stderr, "stderr and stdout cannot "
			"be used together\n");
		ret = EXIT_FAILURE;
		goto exit_stressors_free;
	}

	if (stress_check_temp_path() < 0) {
		ret = EXIT_FAILURE;
		goto exit_stressors_free;
	}

	if (g_opt_flags & OPT_FLAGS_KSM)
		stress_ksm_memory_merge(1);

	/*
	 *  Load in job file options
	 */
	(void)stress_get_setting("job", &job_filename);
	if (stress_parse_jobfile(argc, argv, job_filename) < 0) {
		ret = EXIT_FAILURE;
		goto exit_stressors_free;
	}

	/*
	 *  Sanity check minimize/maximize options
	 */
	if ((g_opt_flags & OPT_FLAGS_MINMAX_MASK) == OPT_FLAGS_MINMAX_MASK) {
		(void)fprintf(stderr, "maximize and minimize cannot "
			"be used together\n");
		ret = EXIT_FAILURE;
		goto exit_stressors_free;
	}

	/*
	 *  Sanity check seq/all settings
	 */
	if (stress_popcount64(g_opt_flags & (OPT_FLAGS_RANDOM | OPT_FLAGS_SEQUENTIAL | OPT_FLAGS_ALL | OPT_FLAGS_PERMUTE)) > 1) {
		(void)fprintf(stderr, "cannot invoke --random, --sequential, --all or --permute "
			"options together\n");
		ret = EXIT_FAILURE;
		goto exit_stressors_free;
	}
	(void)stress_get_setting("class", &class);

	if (class &&
	    !(g_opt_flags & (OPT_FLAGS_SEQUENTIAL | OPT_FLAGS_ALL | OPT_FLAGS_PERMUTE))) {
		(void)fprintf(stderr, "class option is only used with "
			"--sequential, --all or --permute options\n");
		ret = EXIT_FAILURE;
		goto exit_stressors_free;
	}

	/*
	 *  Sanity check mutually exclusive random seed flags
	 */
	if ((g_opt_flags & (OPT_FLAGS_NO_RAND_SEED | OPT_FLAGS_SEED)) ==
	    (OPT_FLAGS_NO_RAND_SEED | OPT_FLAGS_SEED)) {
		(void)fprintf(stderr, "cannot invoke mutually exclusive "
			"--seed and --no-rand-seed options together\n");
		ret = EXIT_FAILURE;
		goto exit_stressors_free;
	}

	/*
	 *  Sanity check --with option
	 */
	if ((g_opt_flags & OPT_FLAGS_WITH) &&
	    ((g_opt_flags & (OPT_FLAGS_SEQUENTIAL | OPT_FLAGS_ALL | OPT_FLAGS_PERMUTE)) == 0)) {
		(void)fprintf(stderr, "the --with option also requires the --seq, --all or --permute options\n");
		ret = EXIT_FAILURE;
		goto exit_stressors_free;
	}

	stress_cpuidle_init();

	/*
	 *  Setup logging
	 */
	if (stress_get_setting("log-file", &log_filename))
		pr_openlog(log_filename);
	shim_openlog("stress-ng", 0, LOG_USER);
	stress_log_args(argc, argv);
	stress_log_system_info();
	stress_log_system_mem_info();
	stress_runinfo();
	stress_buildinfo();
	stress_cpuidle_log_info();
	pr_dbg("%" PRId32 " processor%s online, %" PRId32
		" processor%s configured\n",
		cpus_online, cpus_online == 1 ? "" : "s",
		cpus_configured, cpus_configured == 1 ? "" : "s");

	/*
	 *  For random mode the stressors must be available
	 */
	if (g_opt_flags & OPT_FLAGS_RANDOM)
		stress_enable_all_stressors(0);
	/*
	 *  These two options enable all the stressors
	 */
	if (g_opt_flags & OPT_FLAGS_SEQUENTIAL)
		stress_enable_all_stressors(g_opt_sequential);
	if (g_opt_flags & OPT_FLAGS_ALL)
		stress_enable_all_stressors(g_opt_parallel);
	if (g_opt_flags & OPT_FLAGS_PERMUTE)
		stress_enable_all_stressors(g_opt_permute);
	/*
	 *  Discard stressors that we can't run
	 */
	stress_exclude_unsupported(&unsupported);
	stress_exclude_pathological();
	/*
	 *  Throw away excluded stressors
	 */
	if (stress_exclude() < 0) {
		ret = EXIT_FAILURE;
		goto exit_logging_close;
	}

	/*
	 *  Setup random stressors if requested
	 */
	stress_set_random_stressors();

	stress_ftrace_start();
#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	if (g_opt_flags & OPT_FLAGS_PERF_STATS)
		stress_perf_init();
#endif

	/*
	 *  Setup running environment
	 */
	stress_process_dumpable(false);
	stress_set_oom_adjustment(NULL, false);

	(void)stress_get_setting("ionice-class", &ionice_class);
	(void)stress_get_setting("ionice-level", &ionice_level);
	(void)stress_get_setting("yaml", &yaml_filename);

	stress_mlock_executable();

	/*
	 *  Enable signal handers
	 */
	for (i = 0; i < SIZEOF_ARRAY(stress_signal_map); i++) {
		if (stress_sighandler("stress-ng",
				stress_signal_map[i].signum,
				stress_signal_map[i].handler, NULL) < 0) {
			ret = EXIT_FAILURE;
			goto exit_logging_close;
		}
	}

	/*
	 *  Setup stressor proc info
	 */
	if (g_opt_flags & OPT_FLAGS_SEQUENTIAL) {
		stress_setup_sequential(class, g_opt_sequential);
	} else if (g_opt_flags & OPT_FLAGS_PERMUTE) {
		stress_setup_sequential(class, g_opt_permute);
	} else {
		stress_setup_parallel(class, g_opt_parallel);
	}
	/*
	 *  Seq/parallel modes may have added in
	 *  excluded stressors, so exclude check again
	 */
	stress_exclude_unsupported(&unsupported);
	stress_exclude_pathological();

	stress_set_proc_limits();

	if (!stress_stressor_list.head) {
		pr_err("no stress workers invoked%s\n",
			unsupported ? " (one or more were unsupported)" : "");
		/*
		 *  If some stressors were given but marked as
		 *  unsupported then this is not an error.
		 */
		ret = unsupported ? EXIT_SUCCESS : EXIT_FAILURE;
		stress_settings_show();
		goto exit_logging_close;
	}

	/*
	 *  Allocate shared memory segment for shared data
	 *  across all the child stressors
	 */
	stress_shared_map(stress_get_total_instances(stress_stressor_list.head));

	if (stress_lock_mem_map() < 0) {
		pr_err("failed to create shared heap\n");
		ret = EXIT_FAILURE;
		goto exit_lock_mem_unmap;
	}

	/*
	 *  And now shared memory is created, initialize pr_* lock mechanism
	 */
	if (!stress_shared_heap_init()) {
		pr_err("failed to create shared heap\n");
		ret = EXIT_FAILURE;
		goto exit_shared_unmap;
	}

	if (stress_global_lock_create() < 0) {
		ret = EXIT_FAILURE;
		goto exit_lock_destroy;
	}

	/*
	 *  Assign procs with shared stats memory
	 */
	stress_setup_stats_buffers();

	/*
	 *  Allocate shared cache memory
	 */
	g_shared->mem_cache.size = 0;
	(void)stress_get_setting("cache-size", &g_shared->mem_cache.size);
	g_shared->mem_cache.level = DEFAULT_CACHE_LEVEL;
	(void)stress_get_setting("cache-level", &g_shared->mem_cache.level);
	g_shared->mem_cache.ways = 0;
	(void)stress_get_setting("cache-ways", &g_shared->mem_cache.ways);
	if (stress_cache_alloc("cache allocate") < 0) {
		ret = EXIT_FAILURE;
		goto exit_shared_unmap;
	}

	/*
	 *  Show the stressors we're going to run
	 */
	if (stress_show_stressors() < 0) {
		ret = EXIT_FAILURE;
		goto exit_shared_unmap;
	}

#if defined(STRESS_THERMAL_ZONES)
	/*
	 *  Setup thermal zone data
	 */
	if (g_opt_flags & OPT_FLAGS_TZ_INFO)
		stress_tz_init(&g_shared->tz_info);
#endif

#if defined(STRESS_RAPL)
	if (g_opt_flags & OPT_FLAGS_RAPL_REQUIRED)
		stress_rapl_get_domains(&g_shared->rapl_domains);
#endif

	stress_clear_warn_once();
	stress_stressors_init();

	/* Start thrasher process if required */
	if (g_opt_flags & OPT_FLAGS_THRASH)
		stress_thrash_start();

	stress_vmstat_start();
	stress_smart_start();
	stress_klog_start();
	stress_clocksource_check();
	stress_config_check();
	stress_resctrl_init();

	if (g_opt_flags & OPT_FLAGS_SEQUENTIAL) {
		stress_run_sequential(ticks_per_sec, &duration, &success, &resource_success, &metrics_success);
	} else if (g_opt_flags & OPT_FLAGS_PERMUTE) {
		stress_run_permute(ticks_per_sec, &duration, &success, &resource_success, &metrics_success);
	} else {
		stress_run_parallel(ticks_per_sec, &duration, &success, &resource_success, &metrics_success);
	}

	stress_clocksource_check();

	/* Stop alarms */
	(void)alarm(0);

	/* Stop thasher process */
	if (g_opt_flags & OPT_FLAGS_THRASH)
		stress_thrash_stop();

	yaml = stress_yaml_open(yaml_filename);

	/*
	 *  Dump metrics
	 */
	if (g_opt_flags & OPT_FLAGS_METRICS)
		stress_metrics_dump(yaml);

	if (g_opt_flags & OPT_FLAGS_INTERRUPTS)
		stress_interrupts_dump(yaml, stress_stressor_list.head);

#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	/*
	 *  Dump perf statistics
	 */
	if (g_opt_flags & OPT_FLAGS_PERF_STATS)
		stress_perf_stat_dump(yaml, stress_stressor_list.head, duration);
#endif

#if defined(STRESS_THERMAL_ZONES)
	/*
	 *  Dump thermal zone measurements
	 */
	if (g_opt_flags & OPT_FLAGS_THERMAL_ZONES)
		stress_tz_dump(yaml, stress_stressor_list.head);
	if (g_opt_flags & OPT_FLAGS_TZ_INFO)
		stress_tz_free(&g_shared->tz_info);
#endif
	if (g_opt_flags & OPT_FLAGS_C_STATES)
		stress_cpuidle_dump(yaml, stress_stressor_list.head);

#if defined(STRESS_RAPL)
	if (g_opt_flags & OPT_FLAGS_RAPL)
		stress_rapl_dump(yaml, stress_stressor_list.head, g_shared->rapl_domains);
	if (g_opt_flags & OPT_FLAGS_RAPL_REQUIRED)
		stress_rapl_free_domains(g_shared->rapl_domains);
#endif
	/*
	 *  Dump run times
	 */
	stress_times_dump(yaml, ticks_per_sec, duration);
	stress_exit_status_summary();

	stress_resctrl_deinit();
	stress_klog_stop(&success);
	stress_smart_stop();
	stress_vmstat_stop();
	stress_ftrace_stop();
	stress_ftrace_free();

	pr_inf("%s run completed in %s\n",
		success ? "successful" : "unsuccessful",
		stress_duration_to_str(duration, true, false));

	stress_settings_show();
	/*
	 *  Tidy up
	 */
	stress_global_lock_destroy();
	stress_shared_heap_free();
	stress_stressors_deinit();
	stress_stressors_free();
	stress_cpuidle_free();
	stress_cache_free();
	stress_shared_unmap();
	stress_settings_free();
	stress_lock_mem_unmap();

	/*
	 *  Close logs
	 */
	shim_closelog();
	pr_closelog();
	stress_yaml_close(yaml);

	/*
	 *  Done!
	 */
	if (!success)
		exit(EXIT_NOT_SUCCESS);
	if (!resource_success)
		exit(EXIT_NO_RESOURCE);
	if (!metrics_success)
		exit(EXIT_METRICS_UNTRUSTWORTHY);
	exit(EXIT_SUCCESS);

exit_lock_destroy:
	stress_global_lock_destroy();

exit_shared_unmap:
	stress_shared_unmap();

exit_lock_mem_unmap:
	stress_lock_mem_unmap();

exit_logging_close:
	shim_closelog();
	pr_closelog();

exit_stressors_free:
	stress_stressors_free();

exit_settings_free:
	stress_settings_free();

exit_ret:
	exit(ret);
}
