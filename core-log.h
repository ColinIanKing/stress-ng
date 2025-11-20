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

/* pr_* bit masks, stored in g_pr_log_flags */
#define PR_LOG_FLAGS_ERROR	 STRESS_BIT_ULL(0)	/* Print errors */
#define PR_LOG_FLAGS_INFO	 STRESS_BIT_ULL(1)	/* Print info */
#define PR_LOG_FLAGS_DEBUG	 STRESS_BIT_ULL(2) 	/* Print debug */
#define PR_LOG_FLAGS_FAIL	 STRESS_BIT_ULL(3) 	/* Print test failure message */
#define PR_LOG_FLAGS_WARN	 STRESS_BIT_ULL(4)	/* Print warning */
#define PR_LOG_FLAGS_METRICS	 STRESS_BIT_ULL(5)	/* Print metrics */
#define PR_LOG_FLAGS_STDOUT	 STRESS_BIT_ULL(6)	/* --stdout */
#define PR_LOG_FLAGS_STDERR	 STRESS_BIT_ULL(7)	/* --stdout */
#define PR_LOG_FLAGS_BRIEF	 STRESS_BIT_ULL(8)	/* --log-brief */
#define PR_LOG_FLAGS_LOCKLESS	 STRESS_BIT_ULL(9)	/* --log-lockless */
#define PR_LOG_FLAGS_SKIP_SILENT STRESS_BIT_ULL(10)	/* --skip-silent */
#define PR_LOG_FLAGS_TIMESTAMP   STRESS_BIT_ULL(11)	/* --timestamp */
#define PR_LOG_FLAGS_SYSLOG	 STRESS_BIT_ULL(12)	/* --syslog */

#define PR_LOG_FLAGS_ALL	 (PR_LOG_FLAGS_ERROR | PR_LOG_FLAGS_INFO | \
				  PR_LOG_FLAGS_DEBUG | PR_LOG_FLAGS_FAIL | \
				  PR_LOG_FLAGS_WARN  | PR_LOG_FLAGS_METRICS)

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
