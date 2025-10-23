/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#ifndef CORE_HELPER_H
#define CORE_HELPER_H

#include "stress-ng.h"

/*
 *  stress_warn_once hashes the current filename and line where
 *  the macro is used and returns true if it's never been called
 *  there before across all threads and child processes
 */
#define stress_warn_once()	stress_warn_once_hash(__FILE__, __LINE__)

extern const char ALIGN64 NONSTRING stress_ascii64[64];
extern const char ALIGN64 NONSTRING stress_ascii32[32];

extern WARN_UNUSED int32_t stress_get_processors_online(void);
extern WARN_UNUSED int32_t stress_get_processors_configured(void);
extern WARN_UNUSED int32_t stress_get_ticks_per_second(void);
extern WARN_UNUSED int stress_get_load_avg(double *min1, double *min5, double *min15);
extern void stress_parent_died_alarm(void);
extern int stress_process_dumpable(const bool dumpable);
extern int stress_set_timer_slack_ns(const char *opt);
extern void stress_set_timer_slack(void);
extern void stress_set_proc_name_init(int argc, char *argv[], char *envp[]);
extern void stress_set_proc_name_raw(const char *name);
extern void stress_set_proc_name(const char *name);
extern void stress_set_proc_name_scramble(void);
extern void stress_set_proc_state_str(const char *name, const char *str);
extern void stress_set_proc_state(const char *name, const int state);
extern size_t stress_munge_underscore(char *dst, const char *src, size_t len);
extern WARN_UNUSED int stress_strcmp_munged(const char *s1, const char *s2);
extern WARN_UNUSED uint64_t stress_get_uint64_zero(void);
extern WARN_UNUSED void *stress_get_null(void);
extern WARN_UNUSED bool stress_little_endian(void);
extern void stress_buildinfo(void);
extern void stress_yaml_buildinfo(FILE *yaml);
extern void stress_runinfo(void);
extern void stress_yaml_runinfo(FILE *yaml);
extern WARN_UNUSED unsigned int stress_get_cpu(void);
extern WARN_UNUSED const char *stress_get_compiler(void) RETURNS_NONNULL;
extern WARN_UNUSED const char *stress_get_uname_info(void) RETURNS_NONNULL;
extern WARN_UNUSED int stress_unimplemented(stress_args_t *args);
extern char *stress_uint64_to_str(char *str, size_t len, const uint64_t val,
	const int precisionm, const bool no_zero);
extern WARN_UNUSED char *stress_const_optdup(const char *opt);
extern size_t stress_exec_text_addr(char **start, char **end);
extern WARN_UNUSED bool stress_is_dev_tty(const int fd);
extern WARN_UNUSED bool stress_warn_once_hash(const char *filename, const int line);
extern WARN_UNUSED int stress_get_unused_uid(uid_t *uid);
extern WARN_UNUSED int stress_kernel_release(const int major, const int minor,
	const int patchlevel);
extern WARN_UNUSED int stress_get_kernel_release(void);
extern WARN_UNUSED pid_t stress_get_unused_pid_racy(const bool fork_test);
extern WARN_UNUSED size_t stress_get_hostname_length(void);
extern WARN_UNUSED int stress_get_tty_width(void);
extern WARN_UNUSED bool stress_redo_fork(stress_args_t *args, const int err);
extern void stress_clear_warn_once(void);
extern WARN_UNUSED size_t stress_flag_permutation(const int flags, int **permutations);
extern WARN_UNUSED int stress_exit_status(const int err);
extern WARN_UNUSED char *stress_get_proc_self_exe(char *path, const size_t path_len);
extern WARN_UNUSED int stress_bsd_getsysctl(const char *name, void *ptr, size_t size);
extern WARN_UNUSED uint64_t stress_bsd_getsysctl_uint64(const char *name);
extern WARN_UNUSED uint32_t stress_bsd_getsysctl_uint32(const char *name);
extern WARN_UNUSED unsigned int stress_bsd_getsysctl_uint(const char *name);
extern WARN_UNUSED int stress_bsd_getsysctl_int(const char *name);
extern WARN_UNUSED int stress_x86_readmsr64(const int cpu, const uint32_t reg,
	uint64_t *val);
extern void stress_random_small_sleep(void);
extern void stress_yield_sleep_ms(void);
extern void stress_process_info(stress_args_t *args, const pid_t pid);
extern uint64_t stress_get_machine_id(void);
extern void stress_zero_metrics(stress_metrics_t *metrics, const size_t n);
extern bool OPTIMIZE3 stress_data_is_not_zero(uint64_t *buffer, const size_t len);
extern void stress_no_return(void) NORETURN;

#endif
