/*
 * Copyright (C) 2024      Colin Ian King.
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

#define STRESS_SIGSTKSZ		(stress_get_sig_stack_size())
#define STRESS_MINSIGSTKSZ	(stress_get_min_sig_stack_size())

/*
 *  stress_warn_once hashes the current filename and line where
 *  the macro is used and returns true if it's never been called
 *  there before across all threads and child processes
 */
#define stress_warn_once()	stress_warn_once_hash(__FILE__, __LINE__)

/*
 *  Stack aligning for clone() system calls
 *	align to nearest 16 bytes for aarch64 et al,
 *	assumes we have enough slop to do this
 */
static inline WARN_UNUSED ALWAYS_INLINE void *stress_align_stack(void *stack_top)
{
	return (void *)((uintptr_t)stack_top & ~(uintptr_t)0xf);
}

extern const char ALIGN64 stress_ascii64[64];
extern const char ALIGN64 stress_ascii32[32];

extern void stress_temp_path_free(void);
extern WARN_UNUSED int stress_set_temp_path(const char *path);
extern WARN_UNUSED const char *stress_get_temp_path(void);
extern WARN_UNUSED int stress_check_temp_path(void);
extern size_t stress_mk_filename(char *fullname, const size_t fullname_len,
	const char *pathname, const char *filename);
extern size_t stress_get_page_size(void);
extern WARN_UNUSED int32_t stress_get_processors_online(void);
extern WARN_UNUSED int32_t stress_get_processors_configured(void);
extern WARN_UNUSED int32_t stress_get_ticks_per_second(void);
extern void stress_get_memlimits(size_t *shmall, size_t *freemem,
	size_t *totalmem, size_t *freeswap, size_t *totalswap);
extern void stress_get_gpu_freq_mhz(double *gpu_freq);
extern void stress_ksm_memory_merge(const int flag);
extern WARN_UNUSED bool stress_low_memory(const size_t requested);
extern WARN_UNUSED uint64_t stress_get_phys_mem_size(void);
extern WARN_UNUSED uint64_t stress_get_filesystem_size(void);
extern WARN_UNUSED uint64_t stress_get_filesystem_available_inodes(void);
extern WARN_UNUSED int stress_set_nonblock(const int fd);
extern WARN_UNUSED int stress_get_load_avg(double *min1, double *min5, double *min15);
extern void stress_parent_died_alarm(void);
extern int stress_process_dumpable(const bool dumpable);
extern int stress_set_timer_slack_ns(const char *opt);
extern void stress_set_timer_slack(void);
extern void stress_set_proc_name_init(int argc, char *argv[], char *envp[]);
extern void stress_set_proc_name(const char *name);
extern void stress_set_proc_state_str(const char *name, const char *str);
extern void stress_set_proc_state(const char *name, const int state);
extern size_t stress_munge_underscore(char *dst, const char *src, size_t len);
extern WARN_UNUSED int stress_strcmp_munged(const char *s1, const char *s2);
extern WARN_UNUSED ssize_t stress_get_stack_direction(void);
extern WARN_UNUSED void *stress_get_stack_top(void *start, size_t size);
extern WARN_UNUSED uint64_t stress_get_uint64_zero(void);
extern WARN_UNUSED void *stress_get_null(void);
extern int stress_temp_filename(char *path, const size_t len, const char *name,
	const pid_t pid, const uint32_t instance, const uint64_t magic);
extern int stress_temp_filename_args(stress_args_t *args, char *path,
	const size_t len, const uint64_t magic);
extern int stress_temp_dir(char *path, const size_t len, const char *name,
	const pid_t pid, const uint32_t instance);
extern int stress_temp_dir_args(stress_args_t *args, char *path,
	const size_t len);
extern WARN_UNUSED int stress_temp_dir_mk(const char *name, const pid_t pid,
	const uint32_t instance);
extern WARN_UNUSED int stress_temp_dir_mk_args(stress_args_t *args);
extern int stress_temp_dir_rm(const char *name, const pid_t pid,
	const uint32_t instance);
extern int stress_temp_dir_rm_args(stress_args_t *args);
extern void stress_cwd_readwriteable(void);
extern const char *stress_get_signal_name(const int signum);
extern const char *stress_strsignal(const int signum);
extern WARN_UNUSED bool stress_little_endian(void);
extern void stress_uint8rnd4(uint8_t *data, const size_t len);
extern void stress_runinfo(void);
extern void stress_yaml_runinfo(FILE *yaml);
extern WARN_UNUSED int stress_cache_alloc(const char *name);
extern void stress_cache_free(void);
extern ssize_t stress_system_write(const char *path, const char *buf,
	const size_t buf_len);
extern WARN_UNUSED ssize_t stress_system_read(const char *path, char *buf,
	const size_t buf_len);
extern WARN_UNUSED bool stress_is_prime64(const uint64_t n);
extern WARN_UNUSED uint64_t stress_get_next_prime64(const uint64_t n);
extern WARN_UNUSED uint64_t stress_get_prime64(const uint64_t n);
extern WARN_UNUSED size_t stress_get_max_file_limit(void);
extern WARN_UNUSED size_t stress_get_file_limit(void);
extern WARN_UNUSED int stress_get_bad_fd(void);
extern WARN_UNUSED int stress_sigaltstack_no_check(void *stack, const size_t size);
extern WARN_UNUSED int stress_sigaltstack(void *stack, const size_t size);
extern void stress_sigaltstack_disable(void);
extern WARN_UNUSED int stress_sighandler(const char *name, const int signum,
	void (*handler)(int), struct sigaction *orig_action);
extern WARN_UNUSED int stress_sigchld_set_handler(stress_args_t *args);
extern int stress_sighandler_default(const int signum);
extern void stress_handle_stop_stressing(const int signum);
extern WARN_UNUSED int stress_sig_stop_stressing(const char *name, const int sig);
extern int stress_sigrestore(const char *name, const int signum,
	struct sigaction *orig_action);
extern WARN_UNUSED unsigned int stress_get_cpu(void);
extern WARN_UNUSED const char *stress_get_compiler(void);
extern WARN_UNUSED const char *stress_get_uname_info(void);
extern WARN_UNUSED int stress_unimplemented(stress_args_t *args);
extern WARN_UNUSED size_t stress_probe_max_pipe_size(void);
extern WARN_UNUSED void *stress_align_address(const void *addr, const size_t alignment);
extern WARN_UNUSED bool stress_sigalrm_pending(void);
extern char *stress_uint64_to_str(char *str, size_t len, const uint64_t val);
extern void stress_getset_capability(void);
extern WARN_UNUSED bool stress_check_capability(const int capability);
extern WARN_UNUSED int stress_drop_capabilities(const char *name);
extern WARN_UNUSED bool stress_is_dot_filename(const char *name);
extern WARN_UNUSED char *stress_const_optdup(const char *opt);
extern size_t stress_exec_text_addr(char **start, char **end);
extern WARN_UNUSED bool stress_is_dev_tty(const int fd);
extern void stress_dirent_list_free(struct dirent **dlist, const int n);
extern WARN_UNUSED int stress_dirent_list_prune(struct dirent **dlist, const int n);
extern WARN_UNUSED bool stress_warn_once_hash(const char *filename, const int line);
extern WARN_UNUSED uint16_t stress_ipv4_checksum(uint16_t *ptr, const size_t sz);
extern WARN_UNUSED int stress_get_unused_uid(uid_t *uid);
extern WARN_UNUSED ssize_t stress_read_buffer(const int fd, void* buffer,
	const ssize_t size, const bool ignore_sig_eintr);
extern WARN_UNUSED ssize_t stress_write_buffer(const int fd, const void* buffer,
	const ssize_t size, const bool ignore_sig_eintr);
extern WARN_UNUSED int stress_kernel_release(const int major, const int minor,
	const int patchlevel);
extern WARN_UNUSED int stress_get_kernel_release(void);
extern WARN_UNUSED pid_t stress_get_unused_pid_racy(const bool fork_test);
extern int stress_read_fdinfo(const pid_t pid, const int fd);
extern WARN_UNUSED size_t stress_get_hostname_length(void);
extern WARN_UNUSED size_t stress_get_sig_stack_size(void);
extern WARN_UNUSED size_t stress_get_min_sig_stack_size(void);
extern WARN_UNUSED size_t stress_get_min_pthread_stack_size(void);
extern NORETURN MLOCKED_TEXT void stress_sig_handler_exit(int signum);
extern void stress_set_stack_smash_check_flag(const bool flag);
extern WARN_UNUSED int stress_get_tty_width(void);
extern WARN_UNUSED size_t stress_get_extents(const int fd);
extern WARN_UNUSED bool stress_redo_fork(stress_args_t *args, const int err);
extern void stress_sighandler_nop(int sig);
extern void stress_clear_warn_once(void);
extern WARN_UNUSED size_t stress_flag_permutation(const int flags, int **permutations);
extern WARN_UNUSED const char *stress_get_fs_type(const char *filename);
extern WARN_UNUSED int stress_exit_status(const int err);
extern WARN_UNUSED char *stress_get_proc_self_exe(char *path, const size_t path_len);
extern WARN_UNUSED int stress_bsd_getsysctl(const char *name, void *ptr, size_t size);
extern WARN_UNUSED uint64_t stress_bsd_getsysctl_uint64(const char *name);
extern WARN_UNUSED uint32_t stress_bsd_getsysctl_uint32(const char *name);
extern WARN_UNUSED unsigned int stress_bsd_getsysctl_uint(const char *name);
extern WARN_UNUSED int stress_bsd_getsysctl_int(const char *name);
extern void stress_close_fds(int *fds, const size_t n);
extern void stress_file_rw_hint_short(const int fd);
extern void stress_set_vma_anon_name(const void *addr, const size_t size,
	const char *name);
extern WARN_UNUSED int stress_x86_readmsr64(const int cpu, const uint32_t reg,
	uint64_t *val);
extern void stress_unset_chattr_flags(const char *pathname);
extern int stress_munmap_retry_enomem(void *addr, size_t length);
extern int stress_swapoff(const char *path);
extern void stress_clean_dir(const char *name, const pid_t pid,
	const uint32_t instance);
extern void stress_yield_sleep_ms(void);
extern void stress_catch_sigill(void);
extern void stress_catch_sigsegv(void);
extern void stress_process_info(stress_args_t *args, const pid_t pid);
extern void *stress_mmap_populate(void *addr, size_t length, int prot,
	int flags, int fd, off_t offset);
extern bool stress_addr_readable(const void *addr, const size_t len);
extern uint64_t stress_get_machine_id(void);

#endif
