/*
 * Copyright (C) 2023-2025 Colin Ian King
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
#ifndef CORE_KILLPID_H
#define CORE_KILLPID_H

extern int stress_kill_pid(const pid_t pid);
extern pid_t stress_kill_pid_wait(const pid_t pid, int *status);
extern int stress_kill_sig(const pid_t pid, const int signum);
extern int stress_kill_and_wait(stress_args_t *args, const pid_t pid,
	const int signum, const bool set_stress_force_killed_bogo);
extern int stress_kill_and_wait_many(stress_args_t *args,
	const stress_pid_t *s_pids,
	const size_t n_pids, const int signum,
	const bool set_stress_force_killed_bogo);

#endif
