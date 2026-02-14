/*
 * Copyright (C) 2024-2026 Colin Ian King.
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

#define STRESS_DROP_CACHE_PAGE_CACHE	(0x01)
#define STRESS_DROP_CACHE_SLAB_OBJECTS	(0x02)
#define STRESS_DROP_CACHE_ALL		(STRESS_DROP_CACHE_PAGE_CACHE | \
					 STRESS_DROP_CACHE_SLAB_OBJECTS)

extern WARN_UNUSED const char *stress_fs_temp_path_get(void);
extern WARN_UNUSED int stress_fs_temp_path_check(void);
extern size_t stress_fs_make_filename(char *fullname, const size_t fullname_len,
	const char *pathname, const char *filename);
extern WARN_UNUSED uint64_t stress_fs_size_get(void);
extern WARN_UNUSED uint64_t stress_fs_available_inodes_get(void);
extern void stress_fs_usage_bytes( stress_args_t *args,
	const off_t fs_size_per_instance, const off_t fs_size_total);
extern WARN_UNUSED int stress_fs_nonblocking_set(const int fd);
extern int stress_fs_temp_filename(char *path, const size_t len, const char *name,
	const pid_t pid, const uint32_t instance, const uint64_t magic);
extern int stress_fs_temp_filename_args(stress_args_t *args, char *path,
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
extern ssize_t stress_fs_file_write(const char *path, const char *buf,
	const size_t buf_len);
extern ssize_t stress_fs_discard(const char *path);
extern WARN_UNUSED ssize_t stress_fs_file_read(const char *path, char *buf,
	const size_t buf_len);
extern WARN_UNUSED size_t stress_fs_max_file_limit_get(void);
extern WARN_UNUSED size_t stress_fs_file_limit_get(void);
extern WARN_UNUSED int stress_fs_bad_fd_get(void);
extern WARN_UNUSED bool stress_fs_pipe_check(const int fd);
extern WARN_UNUSED size_t stress_fs_max_pipe_size_get(void);
extern WARN_UNUSED bool stress_fs_filename_dotty(const char *name);
extern void stress_fs_dirent_list_free(struct dirent **dlist, const int n);
extern WARN_UNUSED int stress_fs_dirent_list_prune(struct dirent **dlist, const int n);
extern int stress_fs_fdinfo_read(const pid_t pid, const int fd);
extern WARN_UNUSED size_t stress_fs_extents_get(const int fd);
extern ssize_t stress_fs_read_discard(const int fd);
extern WARN_UNUSED ssize_t stress_fs_read(const int fd, void* buffer,
	const ssize_t size, const bool ignore_sig_eintr);
extern WARN_UNUSED ssize_t stress_fs_write(const int fd, const void* buffer,
	const ssize_t size, const bool ignore_sig_eintr);
extern WARN_UNUSED const char *stress_fs_info_get(const char *filename, uintmax_t *blocks);
extern WARN_UNUSED const char *stress_fs_type_get(const char *filename) RETURNS_NONNULL;
extern void stress_fs_close_fds(int *fds, const size_t n);
extern void stress_fs_file_rw_hint_short(const int fd);
extern void stress_fs_unset_chattr_flags(const char *pathname);
extern void stress_fs_clean_dir(const char *name, const pid_t pid,
	const uint32_t instance);
extern int stress_fs_drop_caches(const int flags);
#endif
