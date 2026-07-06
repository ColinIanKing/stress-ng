/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2026 Colin Ian King.
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
#include "core-capabilities.h"
#include "core-ftrace.h"
#include "core-killpid.h"
#include "core-mounts.h"

#include <ctype.h>

#if defined(HAVE_SYS_TREE_H)
#include <sys/tree.h>
#endif

#if defined(HAVE_SYS_STATFS_H)
#include <sys/statfs.h>
#endif

#if defined(HAVE_BSD_SYS_TREE_H)
#include <bsd/sys/tree.h>
#endif

#if defined(HAVE_LIB_BSD) &&		\
    defined(HAVE_BSD_SYS_TREE_H) &&	\
    defined(RB_ENTRY) &&		\
    defined(__linux__)

#define MAX_MOUNTS	(256)
#if !defined(DEBUGFS_MAGIC)
#define DEBUGFS_MAGIC	(0x64626720)
#endif

struct rb_node {
	RB_ENTRY(rb_node) rb;	/* red/black node entry */
	char *syscall_name;	/* ftrace'd syscall name */
	pid_t syscall_pid;	/* pid of syscall, -1 is use for per-syscall stats */
	uint64_t count;		/* number of calls to func */
	double time_enter;	/* syscall enter time */
	double time_total;	/* syscall return time */
};

#define STATS_PID	(-1)

static volatile bool tracing_run = false;
static bool tracing_enabled = false;
static pid_t tracing_pid = -1;

/*
 *  rb_node_cmp()
 *	used for sorting functions by name and PID
 */
static int rb_node_cmp(struct rb_node *n1, struct rb_node *n2)
{
	const int cmp = strcmp(n1->syscall_name, n2->syscall_name);

	if (cmp)
		return cmp;
	return n1->syscall_pid - n2->syscall_pid;
}

static RB_HEAD(rb_tree, rb_node) rb_root;
RB_PROTOTYPE(rb_tree, rb_node, rb, rb_node_cmp)
RB_GENERATE(rb_tree, rb_node, rb, rb_node_cmp)

/*
 *  stress_ftrace_debugfs_path_get()
 *	find debugfs mount path, returns NULL if not found
 */
static char *stress_ftrace_debugfs_path_get(void)
{
	int i;
	int n;
	char *mnts[MAX_MOUNTS];
	static char debugfs_path[1024];

	/* Cached copy */
	if (*debugfs_path)
		return debugfs_path;

	*debugfs_path = '\0';
	n = stress_mount_get(mnts, MAX_MOUNTS);
	for (i = 0; i < n; i++) {
		struct statfs buf;

		(void)shim_memset(&buf, 0, sizeof(buf));
		if (statfs(mnts[i], &buf) < 0)
			continue;
		if (buf.f_type == DEBUGFS_MAGIC) {
			(void)shim_strscpy(debugfs_path, mnts[i], sizeof(debugfs_path));
			stress_mount_free(mnts, n);
			return debugfs_path;
		}
	}
	stress_mount_free(mnts, n);
	return NULL;
}

/*
 *  stress_ftrace_free()
 *	free up rb tree
 */
void stress_ftrace_free(void)
{
	struct rb_node *tn, *next;

	if (!(g_opt_flags & OPT_FLAGS_FTRACE))
		return;

	for (tn = RB_MIN(rb_tree, &rb_root); tn; tn = next) {
		if (tn->syscall_pid == STATS_PID)
			free(tn->syscall_name);
                next = RB_NEXT(rb_tree, &rb_root, tn);
                RB_REMOVE(rb_tree, &rb_root, tn);
		free(tn);
	}
	RB_INIT(&rb_root);
}

/*
 *  stress_ftrace_tracing_on()
 *	set tracing_on setting to on or off
 */
static int stress_ftrace_tracing_on(bool on)
{
	const char *str = on ? "1" : "0";
	const char *path = stress_ftrace_debugfs_path_get();
	char filename[PATH_MAX];

	if (!path)
		return -1;

	(void)snprintf(filename, sizeof(filename), "%s/tracing/tracing_on", path);
	if (stress_fs_file_write(filename, str, 1) < 0) {
		pr_inf("ftrace: cannot set function tracing, cannot write '%s' to '%s', errno=%d (%s)\n",
			str, filename, errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  stress_ftrace_events_syscalls_enable()
 * 	set events/syscalls/enable to on or off
 */
static int stress_ftrace_events_syscalls_enable(bool on)
{
	const char *str = on ? "1" : "0";
	const char *path = stress_ftrace_debugfs_path_get();
	char filename[PATH_MAX];

	if (!path)
		return -1;

	(void)snprintf(filename, sizeof(filename), "%s/tracing/events/syscalls/enable", path);
	if (stress_fs_file_write(filename, str, 1) < 0) {
		pr_inf("ftrace: cannot set syscall events file, cannot write '%s' to '%s', errno=%d (%s)\n",
			str, filename, errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  stress_ftrace_current_tracer()
 *	set current_tracer to str setting, report error if carp is true
 */
static int stress_ftrace_current_tracer(const char *str, bool carp)
{
	const char *path = stress_ftrace_debugfs_path_get();
	char filename[PATH_MAX];

	(void)snprintf(filename, sizeof(filename), "%s/tracing/current_tracer", path);
	if (stress_fs_file_write(filename, str, strlen(str)) < 0) {
		if (carp) {
			pr_inf("ftrace: cannot set function tracing, cannot write '%s' to '%s', errno=%d (%s)\n",
				str, filename, errno, strerror(errno));
		}
		return -1;
	}
	return 0;
}

static void stress_ftrace_sig_handler(int sig)
{
	(void)sig;

	tracing_run = false;
}

static void stress_ftrace_child(FILE *fp)
{
	char buf[1024];
	const pid_t my_pid = getpid();
	const pid_t ppid = getppid();
	struct rb_node node;
	struct rb_node *tn;
	struct rb_node *next;
	uint64_t sys_calls = 0;

	RB_INIT(&rb_root);

	(void)memset(buf, 0, sizeof(buf));
	do {
		char *ptr;
		char *hyphen;
		char *syscall_name;
		char ch;
		bool syscall_enter;
		pid_t syscall_pid;
		double time_stamp;

		/*
		 *   stress-ng-foo-1884998 [007] ..... 191177.314874: sys_ppoll(....)
		 *   stress-ng-foo-1884998 [007] ..... 191177.314876: sys_ppoll -> 0x1
		 */
		if (fgets(buf, sizeof(buf), fp) == NULL)
			break;

		/* skip over any leading spaces */
		ptr = buf;
		while (*ptr == ' ')
			ptr++;
		if (!*ptr)
			continue;

		if (strncmp("stress-ng-", ptr, 9))
			continue;

		/* skip stress-ng-$PID field */
		hyphen = NULL;
		while (*ptr == ' ')
			ptr++;
		if (!*ptr)
			continue;
		while ((ch = *ptr) && ch != ' ') {
			if (ch == '-')
				hyphen = ptr;
			ptr++;
		}
		if ((!*ptr) || (!hyphen))
			continue;
		hyphen++;
		syscall_pid = atol(hyphen);

		/* ignore tracer child and stress-ng parent */
		if ((syscall_pid == my_pid) || (syscall_pid == ppid))
			continue;

		/* skip over [cpu] field */
		while (*ptr == ' ')
			ptr++;
		if (!*ptr)
			continue;
		while (*ptr != ' ')
			ptr++;
		if (!*ptr)
			continue;

		/* skip over ..... field */
		while (*ptr == ' ')
			ptr++;
		if (!*ptr)
			continue;
		while (*ptr != ' ')
			ptr++;
		if (!*ptr)
			continue;

		time_stamp = 0.0;
		if (sscanf(ptr, "%lf", &time_stamp) != 1)
			continue;

		/* skip over time stamp field */
		while (*ptr == ' ')
			ptr++;
		if (!*ptr)
			continue;
		while (*ptr != ' ')
			ptr++;
		if (!*ptr)
			continue;

		/* find syscall */
		while (*ptr == ' ')
			ptr++;
		if (!*ptr)
			continue;

		syscall_name = ptr;
		while ((ch = *ptr) && (ch != '(') && (ch != ' '))
			ptr++;

		if (!ch)
			continue;

		*ptr = '\0';
		syscall_enter = (ch == '(');
		node.syscall_name = syscall_name;
		node.syscall_pid = syscall_pid;

		tn = RB_FIND(rb_tree, &rb_root, &node);
		if (tn) {
			if (syscall_enter) {
				/* save entry time */
				tn->time_enter = time_stamp;
			} else {
				struct rb_node *pidless_node;
				const double duration = (time_stamp > tn->time_enter) ?
					time_stamp - tn->time_enter : 0.0;

				/*
				 *  Try to find pidless acconting node
				 */
				node.syscall_name = syscall_name;
				node.syscall_pid = STATS_PID;
				pidless_node = RB_FIND(rb_tree, &rb_root, &node);
				if (pidless_node) {
					pidless_node->time_total += duration;
					pidless_node->count++;
					RB_REMOVE(rb_tree, &rb_root, tn);
					free(tn);
				}

			}
		} else {
			struct rb_node *new_node;
			struct rb_node *pidless_node;
			char *dup_syscall_name;

			/*
			 *  see if a PID-less counter exists, allocate if
			 *  not.
			 */
			node.syscall_name = syscall_name;
			node.syscall_pid = STATS_PID;
			pidless_node = RB_FIND(rb_tree, &rb_root, &node);
			if (pidless_node) {
				/* re-use from PID-less node */
				dup_syscall_name = pidless_node->syscall_name;
			} else {
				dup_syscall_name = strdup(syscall_name);
				if (!dup_syscall_name)
					continue;

				new_node = (struct rb_node *)calloc(1, sizeof(*new_node));
				if (!new_node) {
					free(dup_syscall_name);
					continue;
				}
				new_node->syscall_name = dup_syscall_name;
				new_node->syscall_pid = STATS_PID;
				new_node->count = 0;
				new_node->time_total = 0.0;
				if (RB_INSERT(rb_tree, &rb_root, new_node) != NULL) {
					free(new_node->syscall_name);
					free(new_node);
					continue;
				}
			}

			/*
			 *  now alloctate a node for this specific PID
			 */
			new_node = (struct rb_node *)calloc(1, sizeof(*new_node));
			if (!new_node)
				continue;
			new_node->syscall_name = dup_syscall_name;
			new_node->syscall_pid = syscall_pid;
			new_node->count = 0;
			new_node->time_enter = time_stamp;
			new_node->time_total = 0.0;
			if (RB_INSERT(rb_tree, &rb_root, new_node) != NULL) {
				free(tn);
				continue;
			}
		}
	} while (tracing_run && stress_continue_flag());

	(void)stress_ftrace_tracing_on(false);

	pr_inf("ftrace: %-30.30s %15.15s %20.20s\n", "System Call", "Number of Calls", "Total Time (ms)");

	/*
	 *  scan tree for syscalls where return has not been
	 *  accounted for and add these to the call count
	 */
	for (tn = RB_MIN(rb_tree, &rb_root); tn; tn = next) {
		if (tn->syscall_pid != STATS_PID) {
			struct rb_node *pidless_node;

			node.syscall_name = tn->syscall_name;
			node.syscall_pid = STATS_PID;
			pidless_node = RB_FIND(rb_tree, &rb_root, &node);
			if (pidless_node) {
				pidless_node->count += tn->count;
			}
		}
                next = RB_NEXT(rb_tree, &rb_root, tn);
	}

	/*
	 *  and dump totals for each syscall
	 */
	for (tn = RB_MIN(rb_tree, &rb_root); tn; tn = next) {
		const int64_t count = tn->count;

		if ((count > 0) && (tn->syscall_pid == STATS_PID)) {
			pr_inf("ftrace: %-30.30s %15" PRIu64 " %20.2f\n", tn->syscall_name, count, tn->time_total * 1000000.0);
			sys_calls++;
		}
                next = RB_NEXT(rb_tree, &rb_root, tn);
	}
	pr_inf("ftrace: %" PRIu64 " system calls were traced\n", sys_calls);
}

/*
 *  stress_ftrace_start()
 *	start ftracing function calls
 */
void stress_ftrace_start(void)
{
	FILE *fp;
	const char *path;
	char filename[PATH_MAX];

	tracing_enabled = false;
	tracing_run = false;
	tracing_pid = -1;

	if (!(g_opt_flags & OPT_FLAGS_FTRACE))
		return;

	if (!stress_capabilities_check(SHIM_CAP_SYS_ADMIN)) {
		pr_inf("ftrace: requires CAP_SYS_ADMIN capability for tracing\n");
		return;
	}

	path = stress_ftrace_debugfs_path_get();
	if (!path) {
		pr_inf("ftrace: cannot find a mounted debugfs\n");
		return;
	}

	/* force tracing off */
	if (stress_ftrace_tracing_on(false) < 0)
		return;

	/* reset tracing options */
	(void)snprintf(filename, sizeof(filename), "%s/tracing/options/function-trace", path);
	if (stress_fs_file_write(filename, "0", 1) < 0) {
		pr_inf("ftrace: cannot reset function profiling, cannot write to '%s', errno=%d (%s), "
			"ensure CONFIG_FUNCTION_PROFILER=y\n",
			filename, errno, strerror(errno));
		(void)stress_ftrace_tracing_on(false);
		return;
	}

	/* select nop tracer */
	if (stress_ftrace_current_tracer("nop", true) < 0) {
		(void)stress_ftrace_tracing_on(false);
		return;
	}

	/* empty trace buffer */
	(void)snprintf(filename, sizeof(filename), "%s/tracing/trace", path);
	if (stress_fs_file_write(filename, "\n", 1) < 0) {
		pr_inf("ftrace: cannot empty trace buffer file '%s', errno=%d (%s)\n",
			filename, errno, strerror(errno));
		return;
	}

	/* enable syscall events */
	if (stress_ftrace_events_syscalls_enable(true) < 0)
		return;

	/* enable tracing */
	if (stress_ftrace_tracing_on(true) < 0) {
		(void)stress_ftrace_tracing_on(false);
		(void)stress_ftrace_events_syscalls_enable(false);
		return;
	}

	(void)snprintf(filename, sizeof(filename), "%s/tracing/trace_pipe", path);
	fp = fopen(filename, "r");
	if (!fp) {
		pr_inf("ftrace: cannot open '%s', errno=%d (%s), "
			"ensure CONFIG_FUNCTION_PROFILER=y\n",
			filename, errno, strerror(errno));
		(void)stress_ftrace_tracing_on(false);
		(void)stress_ftrace_events_syscalls_enable(false);
		return;
	}

#if defined(HAVE_SETVBUF)
	/* try to use 1 MB buffer */
	(void)setvbuf(fp, NULL, _IOFBF, MB);
#endif

	tracing_run = true;
	tracing_pid = fork();
	if (tracing_pid < 0) {
		(void)fclose(fp);
		pr_inf("ftrace: failed to fork, disabing function profiling\n");
		tracing_run = false;
		return;
	} else if (tracing_pid == 0) {
		if (stress_signal_handler("ftrace", SIGALRM, stress_ftrace_sig_handler, NULL) < 0)
			_exit(1);
		if (stress_signal_handler("ftrace", SIGINT, stress_ftrace_sig_handler, NULL) < 0)
			_exit(1);
		stress_ftrace_child(fp);
		(void)fclose(fp);
		_exit(0);
	} else {
		(void)fclose(fp);
	}
	tracing_enabled = true;
}

/*
 *  stress_ftrace_stop()
 *	stop ftracing function calls and analyze the collected
 *	stats
 */
void stress_ftrace_stop(void)
{
	const char *path;
	int status;

	tracing_run = false;

	if (!(g_opt_flags & OPT_FLAGS_FTRACE))
		return;

	if (!tracing_enabled)
		return;

	if (tracing_pid < 0)
		return;

	(void)kill(tracing_pid, SIGALRM);
	(void)shim_waitpid(tracing_pid, &status, 0);

	path = stress_ftrace_debugfs_path_get();
	if (!path)
		return;

	(void)stress_ftrace_tracing_on(false);
	(void)stress_ftrace_events_syscalls_enable(false);
}

#else
void stress_ftrace_free(void)
{
}

void stress_ftrace_start(void)
{
	if (!(g_opt_flags & OPT_FLAGS_FTRACE))
		return;
	pr_inf("ftrace: this option is not implemented on this system: %s %s, "
		"requires CONFIG_FUNCTION_PROFILER=y and sys/tree.h",
		stress_uname_info_get(), stress_compiler_get());
}

void stress_ftrace_stop(void)
{
}
#endif
