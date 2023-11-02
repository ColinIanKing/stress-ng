/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C)      2023 Colin Ian King
 *
 */
#ifndef CORE_KILLPID_H
#define CORE_KILLPID_H

extern int stress_kill_pid(const pid_t pid);
extern int stress_kill_pid_wait(const pid_t pid, int *status);
extern int stress_kill_sig(const pid_t pid, const int signum);
extern int stress_kill_and_wait(const stress_args_t *args, const pid_t pid,
	const int signum, const bool set_stress_force_killed_bogo);
extern int stress_kill_and_wait_many(const stress_args_t *args, const pid_t *pids,
	const size_t n_pids, const int signum,
	const bool set_stress_force_killed_bogo);

#endif
