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
#ifndef CORE_SIGNAL_H
#define CORE_SIGNAL_H

#include "stress-ng.h"

extern const char *stress_signal_name(const int signum);
extern const char *stress_signal_str(const int signum) RETURNS_NONNULL;
extern void stress_signal_longjump_mask(sigset_t *set);
extern WARN_UNUSED int stress_sighandler(const char *name, const int signum,
	void (*handler)(int), struct sigaction *orig_action);
extern WARN_UNUSED int stress_sigchld_set_handler(stress_args_t *args);
extern void stress_signal_stop_flag_handler(int sig);
extern int stress_signal_default_handler(const int signum);
extern void stress_signal_stop_stressing_realarm(const int signum);
extern WARN_UNUSED int stress_signal_stop_stressing(const char *name, const int sig);
extern int stress_signal_restore(const char *name, const int signum,
	struct sigaction *orig_action);
extern WARN_UNUSED bool stress_signal_alrm_pending(void);
extern NORETURN void stress_signal_exit_handler(int signum);
extern void stress_signal_ignore_handler(int sig);
extern void stress_signal_catch_sigill(void);
extern void stress_signal_catch_sigsegv(void);

#endif
