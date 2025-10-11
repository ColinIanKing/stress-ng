/*
 * Copyright (C) 2025      Colin Ian King.
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

#ifndef STRESS_CORE_SYNC_H
#define STRESS_CORE_SYNC_H

#define STRESS_SYNC_START_FLAG_WAITING		(0)
#define STRESS_SYNC_START_FLAG_STARTED		(1)
#define STRESS_SYNC_START_FLAG_RUNNING		(2)
#define STRESS_SYNC_START_FLAG_FINISHED		(3)

extern stress_pid_t *stress_sync_s_pids_mmap(const size_t num);
extern int stress_sync_s_pids_munmap(stress_pid_t *s_pids, const size_t num);
extern void stress_sync_start_init(stress_pid_t *s_pid);
extern void stress_sync_start_wait_s_pid(stress_pid_t *s_pid);
extern void stress_sync_start_wait(stress_args_t *args);
extern void stress_sync_start_cont_s_pid(stress_pid_t *s_pid);
extern void stress_sync_start_cont_list(stress_pid_t *s_pids_head);

/*
 *  stress_sync_state_store()
 *	store the stress_pids_t state, try and use atomic updates where
 *	possible. non-atomic state changes are OK, but can require
 *	additional re-polled read loops so are less optimal when
 *	reading state changes
 */
static inline ALWAYS_INLINE void stress_sync_state_store(stress_pid_t *s_pid, uint8_t state)
{
#if defined(HAVE_ATOMIC_STORE) && 	\
    defined(__ATOMIC_SEQ_CST)
	__atomic_store(&s_pid->state, &state, __ATOMIC_SEQ_CST);
#else
	/* racy alternative */
	s_pid->state = state;
#endif
}

/*
 *  stress_sync_state_load()
 *	load the stress_pid_state
 */
static inline ALWAYS_INLINE void stress_sync_state_load(stress_pid_t *s_pid, uint8_t *state)
{
#if defined(HAVE_ATOMIC_LOAD) &&	\
    defined(__ATOMIC_SEQ_CST)
	__atomic_load(&s_pid->state, state, __ATOMIC_SEQ_CST);
#else
	/* racy alternative */
	*state = s_pid->state;
#endif
}

/*
 *  stress_sync_start_s_pid_list_add()
 *	add s_pid to pid list head
 */
static inline ALWAYS_INLINE void stress_sync_start_s_pid_list_add(
	stress_pid_t **s_pids_head,
	stress_pid_t *s_pid)
{
	s_pid->next = *s_pids_head;
	*s_pids_head = s_pid;
}

#endif
