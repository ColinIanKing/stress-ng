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
#include "core-capabilities.h"
#include "core-ftrace.h"
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
	char *func_name;	/* ftrace'd kernel function name */
	int64_t start_count;	/* start number of calls to func */
	int64_t end_count;	/* end number of calls to func */
	double	start_time_us;	/* start time used by func in microsecs */
	double	end_time_us;	/* end time used by func microsecs */
};

static bool tracing_enabled;

/*
 *  rb_node_cmp()
 *	used for sorting functions by name
 */
static int rb_node_cmp(struct rb_node *n1, struct rb_node *n2)
{
	return strcmp(n1->func_name, n2->func_name);
}

static RB_HEAD(rb_tree, rb_node) rb_root;
RB_PROTOTYPE(rb_tree, rb_node, rb, rb_node_cmp);
RB_GENERATE(rb_tree, rb_node, rb, rb_node_cmp);

/*
 *  stress_ftrace_get_debugfs_path()
 *	find debugfs mount path, returns NULL if not found
 */
static char *stress_ftrace_get_debugfs_path(void)
{
	int i, n;
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
		free(tn->func_name);
                next = RB_NEXT(rb_tree, &rb_root, tn);
                RB_REMOVE(rb_tree, &rb_root, tn);
		free(tn);
	}
	RB_INIT(&rb_root);
}

/*
 *  stress_ftrace_parse_trace_stat_file()
 *	parse the ftrace files for function timing stats
 */
static int stress_ftrace_parse_trace_stat_file(const char *path, const bool start)
{
	FILE *fp;
	char buffer[4096];

	fp = fopen(path, "r");
	if (!fp)
		return 0;

	while (fgets(buffer, sizeof(buffer), fp) != NULL) {
		struct rb_node *tn, node;
		char *ptr, *func_name;
		const char *num;
		int64_t count;
		double time_us;

		if (strstr(buffer, "Function"))
			continue;
		if (strstr(buffer, "----"))
			continue;

		/*
		 *  Skip over leading spaces and find function name
		 */
		for (ptr = buffer; *ptr && isspace(*ptr); ptr++)
			;
		if (!*ptr)
			continue;
		func_name = ptr;

		/*
		 *  Skip over leading spaces and find hit count
		 */
		for (; *ptr && !isspace(*ptr); ptr++)
			;
		if (!*ptr)
			continue;
		*ptr++ = '\0';
		for (; *ptr && isspace(*ptr); ptr++)
			;
		num = ptr;
		for (; *ptr && !isspace(*ptr); ptr++)
			;
		if (!*ptr)
			continue;
		*ptr++ = '\0';
		count = (int64_t)atoll(num);

		/*
		 *  Skip over leading spaces and find time consumed
		 */
		for (; *ptr && isspace(*ptr); ptr++)
			;
		if (!*ptr)
			continue;
		if (sscanf(ptr, "%lf", &time_us) != 1)
			time_us = 0.0;

		node.func_name = func_name;

		tn = RB_FIND(rb_tree, &rb_root, &node);
		if (tn) {
			if (start) {
				tn->start_count += count;
				tn->start_time_us += time_us;
			} else {
				tn->end_count += count;
				tn->end_time_us += time_us;
			}
		} else {
			tn = (struct rb_node *)malloc(sizeof(*tn));
			if (UNLIKELY(!tn))
				goto memory_fail;
			tn->func_name = shim_strdup(func_name);
			if (UNLIKELY(!tn->func_name)) {
				free(tn);
				goto memory_fail;
			}
			tn->start_count = 0;
			tn->end_count = 0;
			tn->start_time_us = 0.0;
			tn->end_time_us = 0.0;

			if (start) {
				tn->start_count = count;
				tn->start_time_us = time_us;
			} else {
				tn->end_count = count;
				tn->end_time_us = time_us;
			}
			/* If we find an exiting matching, free the unused new tn */
			if (RB_INSERT(rb_tree, &rb_root, tn) != NULL)
				free(tn);
		}
	}
	(void)fclose(fp);
	return 0;

memory_fail:
	(void)fclose(fp);
	pr_inf("ftrace: disabled, out of memory collecting function information\n");
	stress_ftrace_free();
	return -1;
}

/*
 *  stress_ftrace_parse_stat_files()
 *	read trace stat files and parse the data into the rb tree
 */
static int stress_ftrace_parse_stat_files(const char *path, const bool start)
{
	DIR *dp;
	const struct dirent *de;
	char filename[PATH_MAX];

	(void)snprintf(filename, sizeof(filename), "%s/tracing/trace_stat", path);
	dp = opendir(filename);
	if (!dp)
		return -1;
	while ((de = readdir(dp)) != NULL) {
		if (strncmp(de->d_name, "function", 8) == 0) {
			char funcfile[PATH_MAX];

			(void)snprintf(funcfile, sizeof(funcfile),
				"%s/tracing/trace_stat/%s", path, de->d_name);
			stress_ftrace_parse_trace_stat_file(funcfile, start);
		}
	}
	(void)closedir(dp);

	return 0;
}

/*
 *  stress_ftrace_add_pid()
 *	enable/append/stop tracing on specific events.
 *	if pid < 0 then tracing pids are all removed otherwise
 *	the pid is added to the tracing events
 */
void stress_ftrace_add_pid(const pid_t pid)
{
	char filename[PATH_MAX];
	const char *path;
	char buffer[32];
	int fd;

	if (!(g_opt_flags & OPT_FLAGS_FTRACE))
		return;

	path = stress_ftrace_get_debugfs_path();
	if (!path)
		return;

	(void)snprintf(filename, sizeof(filename), "%s/tracing/set_ftrace_pid", path);
	fd = open(filename, O_WRONLY | (pid < 0 ? O_TRUNC :  O_APPEND));
	if (fd < 0)
		return;
	if (pid == -1) {
		strcpy(buffer, " ");
	} else {
		(void)snprintf(buffer, sizeof(buffer), "%" PRIdMAX , (intmax_t)pid);
	}
	VOID_RET(ssize_t, write(fd, buffer, strlen(buffer)));
	(void)close(fd);
}

/*
 *  stress_ftrace_start()
 *	start ftracing function calls
 */
void stress_ftrace_start(void)
{
	const char *path;
	char filename[PATH_MAX];

	if (!(g_opt_flags & OPT_FLAGS_FTRACE))
		return;

	RB_INIT(&rb_root);

	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf("ftrace: requires CAP_SYS_ADMIN capability for tracing\n");
		return;
	}

	path = stress_ftrace_get_debugfs_path();
	if (!path) {
		pr_inf("ftrace: cannot find a mounted debugfs\n");
		return;
	}

	(void)snprintf(filename, sizeof(filename), "%s/tracing/function_profile_enabled", path);
	if (stress_system_write(filename, "0", 1) < 0) {
		pr_inf("ftrace: cannot enable function profiling, cannot write to '%s', errno=%d (%s)\n",
			filename, errno, strerror(errno));
		return;
	}
	stress_ftrace_add_pid(-1);
	stress_ftrace_add_pid(getpid());
	(void)snprintf(filename, sizeof(filename), "%s/tracing/function_profile_enabled", path);
	if (stress_system_write(filename, "1", 1) < 0) {
		pr_inf("ftrace: cannot enable function profiling, cannot write to '%s', errno=%d (%s)\n",
			filename, errno, strerror(errno));
		return;
	}
	if (stress_ftrace_parse_stat_files(path, true) < 0)
		return;

	tracing_enabled = true;
}

/*
 *  strace_ftrace_is_syscall()
 *	return true if function name looks like a system call
 */
static inline bool CONST strace_ftrace_is_syscall(const char *func_name)
{
	if (*func_name == '_' &&
	    strstr(func_name, "_sys_") &&
	    !strstr(func_name, "do_sys") &&
	    strncmp(func_name, "___", 3))
		return true;

	return false;
}

/*
 *  stress_ftrace_analyze()
 *	dump ftrace analysis
 */
static void stress_ftrace_analyze(void)
{
	struct rb_node *tn, *next;
	uint64_t sys_calls = 0, func_calls = 0;

	pr_inf("ftrace: %-30.30s %15.15s %20.20s\n", "System Call", "Number of Calls", "Total Time (us)");

	for (tn = RB_MIN(rb_tree, &rb_root); tn; tn = next) {
		int64_t count = tn->end_count - tn->start_count;

		if (count > 0) {
			func_calls++;
			if (strace_ftrace_is_syscall(tn->func_name)) {
				double time_us = tn->end_time_us -
						 tn->start_time_us;

				pr_inf("ftrace: %-30.30s %15" PRIu64 " %20.2f\n", tn->func_name, count, time_us);
				sys_calls++;
			}
		}

                next = RB_NEXT(rb_tree, &rb_root, tn);
	}
	pr_inf("ftrace: %" PRIu64 " kernel functions called, %" PRIu64 " were system calls\n",
		func_calls, sys_calls);
}

/*
 *  stress_ftrace_stop()
 *	stop ftracing function calls and analyze the collected
 *	stats
 */
void stress_ftrace_stop(void)
{
	const char *path;
	char filename[PATH_MAX];

	if (!(g_opt_flags & OPT_FLAGS_FTRACE))
		return;

	if (!tracing_enabled)
		return;

	path = stress_ftrace_get_debugfs_path();
	if (!path)
		return;

	stress_ftrace_add_pid(-1);
	(void)snprintf(filename, sizeof(filename), "%s/tracing/function_profile_enabled", path);
	if (stress_system_write(filename, "0", 1) < 0) {
		pr_inf("ftrace: cannot disable function profiling, errno=%d (%s)\n",
			errno, strerror(errno));
		return;
	}

	(void)snprintf(filename, sizeof(filename), "%s/tracing/trace_stat", path);
	if (stress_ftrace_parse_stat_files(path, false) < 0)
		return;
	stress_ftrace_analyze();
}

#else
void stress_ftrace_add_pid(const pid_t pid)
{
	(void)pid;
}

void stress_ftrace_free(void)
{
}

void stress_ftrace_start(void)
{
	if (!(g_opt_flags & OPT_FLAGS_FTRACE))
		return;
	pr_inf("ftrace: this option is not implemented on this system: %s %s\n",
		stress_get_uname_info(), stress_get_compiler());
}

void stress_ftrace_stop(void)
{
}
#endif
