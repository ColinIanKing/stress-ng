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
#ifndef CORE_LOG_H
#define CORE_LOG_H

#include "core-attribute.h"

extern WARN_UNUSED int pr_fd(void);
extern void pr_block_begin(void);
extern void pr_block_end(void);
extern void pr_fail_check(int *const rc);
extern int pr_yaml(FILE *fp, const char *const fmt, ...) FORMAT(printf, 2, 3);
extern void pr_closelog(void);
extern void pr_openlog(const char *filename);
extern void pr_dbg(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_dbg_skip(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_inf(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_inf_skip(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_err(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_err_skip(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_fail(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_tidy(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_warn(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_warn_skip(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_metrics(const char *fmt, ...)	FORMAT(printf, 1, 2);

#endif
