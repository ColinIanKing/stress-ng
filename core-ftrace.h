/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#ifndef CORE_FTRACE_H
#define CORE_FTRACE_H

/* ftrace helpers */
extern int stress_ftrace_start(void);
extern void stress_ftrace_stop(void);
extern void stress_ftrace_free(void);
extern void stress_ftrace_add_pid(const pid_t pid);

#endif
