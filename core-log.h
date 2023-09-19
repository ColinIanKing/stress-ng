/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_LOG_H
#define CORE_LOG_H

#include "core-attribute.h"

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
