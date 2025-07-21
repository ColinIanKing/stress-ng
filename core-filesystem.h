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
#ifndef CORE_FILESYSTEM_H
#define CORE_FILESYSTEM_H

#include "stress-ng.h"

extern WARN_UNUSED const char *stress_get_temp_path(void);
extern WARN_UNUSED int stress_check_temp_path(void);
extern size_t stress_mk_filename(char *fullname, const size_t fullname_len,
	const char *pathname, const char *filename);
extern WARN_UNUSED uint64_t stress_get_filesystem_size(void);
extern WARN_UNUSED uint64_t stress_get_filesystem_available_inodes(void);
extern void stress_fs_usage_bytes( stress_args_t *args,
	const off_t fs_size_per_instance, const off_t fs_size_total);
extern WARN_UNUSED int stress_set_nonblock(const int fd);
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
extern ssize_t stress_system_write(const char *path, const char *buf,
	const size_t buf_len);
extern ssize_t stress_system_discard(const char *path);
extern WARN_UNUSED ssize_t stress_system_read(const char *path, char *buf,
	const size_t buf_len);
extern WARN_UNUSED size_t stress_get_max_file_limit(void);
extern WARN_UNUSED size_t stress_get_file_limit(void);
extern WARN_UNUSED int stress_get_bad_fd(void);
extern WARN_UNUSED bool stress_is_a_pipe(const int fd);
extern WARN_UNUSED size_t stress_probe_max_pipe_size(void);
extern WARN_UNUSED bool stress_is_dot_filename(const char *name);
extern void stress_dirent_list_free(struct dirent **dlist, const int n);
extern WARN_UNUSED int stress_dirent_list_prune(struct dirent **dlist, const int n);
extern int stress_read_fdinfo(const pid_t pid, const int fd);
extern WARN_UNUSED size_t stress_get_extents(const int fd);
extern ssize_t stress_read_discard(const int fd);
extern WARN_UNUSED ssize_t stress_read_buffer(const int fd, void* buffer,
	const ssize_t size, const bool ignore_sig_eintr);
extern WARN_UNUSED ssize_t stress_write_buffer(const int fd, const void* buffer,
	const ssize_t size, const bool ignore_sig_eintr);
extern WARN_UNUSED const char *stress_get_fs_info(const char *filename, uintmax_t *blocks);
extern WARN_UNUSED const char *stress_get_fs_type(const char *filename) RETURNS_NONNULL;
extern void stress_close_fds(int *fds, const size_t n);
extern void stress_file_rw_hint_short(const int fd);
extern void stress_unset_chattr_flags(const char *pathname);
extern void stress_clean_dir(const char *name, const pid_t pid,
	const uint32_t instance);
#endif
