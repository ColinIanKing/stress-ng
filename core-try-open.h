/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_TRY_OPEN_H
#define CORE_TRY_OPEN_H

extern int stress_try_open(const stress_args_t *args, const char *path,
	const int flags, const unsigned long timeout_ns);
extern int stress_open_timeout(const char *name, const char *path,
	const int flags, const unsigned long timeout_ns);

#endif
