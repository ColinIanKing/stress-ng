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

#include "stress-ng.h"
#include "core-sync.h"

/*
 *  stress_sync_start_timeout()
 *	set the timeout for SIGALRM for a stressor
 */
static void stress_sync_start_timeout(void)
{
	if (g_opt_timeout)
		(void)alarm((unsigned int)g_opt_timeout);
}

/*
 *  stress_sync_s_pids_mmap()
 *	mmap an array of stress_pids_t of num elements; these need
 *	to map as shared so stressor and parent can load/store the
 *	state setting.
 */
stress_pid_t *stress_sync_s_pids_mmap(const size_t num)
{
	stress_pid_t *s_pids;
	const size_t size = num * sizeof(stress_pid_t);

	s_pids = (stress_pid_t *)mmap(NULL, size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (s_pids != MAP_FAILED)
		stress_set_vma_anon_name(s_pids, size, "s_pids");
	return s_pids;
}

/*
 *  stress_sync_s_pids_munmap()
 *	unmap num sized stress_pids_t array
 */
int stress_sync_s_pids_munmap(stress_pid_t *s_pids, const size_t num)
{
	return munmap((void *)s_pids, num * sizeof(stress_pid_t));
}

/*
 *  stress_sync_start_init()
 *	initialize the stress_pid_t state
 */
void stress_sync_start_init(stress_pid_t *s_pid)
{
	s_pid->pid = -1;
	stress_sync_state_store(s_pid, STRESS_SYNC_START_FLAG_STARTED);
}

/*
 *  stress_sync_start_wait_s_pid()
 *	put process into a stop (waiting) state, will be
 *	woken up by a parent call to stress_sync_start_cont_s_pid()
 */
void stress_sync_start_wait_s_pid(stress_pid_t *s_pid)
{
	pid_t pid;

	if (!(g_opt_flags & OPT_FLAGS_SYNC_START))
		return;

	pid = s_pid->oomable_child ? s_pid->oomable_child : s_pid->pid;
	if (pid <= 1)
		return;

	stress_sync_state_store(s_pid, STRESS_SYNC_START_FLAG_WAITING);
	if (kill(pid, SIGSTOP) < 0) {
		pr_inf("cannot stop stressor on for --sync-start, errno=%d (%s)",
			errno, strerror(errno));
	}
	stress_sync_state_store(s_pid, STRESS_SYNC_START_FLAG_RUNNING);
	stress_sync_start_timeout();
}

/*
 *  stress_sync_start_wait()
 *	put stressor into a stop (waiting) state, will be
 *	woken up by a parent call to stress_sync_start_cont_s_pid()
 */
void stress_sync_start_wait(stress_args_t *args)
{
	pid_t pid;
	stress_pid_t *s_pid;

	if (!(g_opt_flags & OPT_FLAGS_SYNC_START))
		return;

	s_pid = &args->stats->s_pid;
	pid = s_pid->oomable_child ? s_pid->oomable_child : s_pid->pid;
	if (pid <= 1)
		return;

	stress_sync_state_store(s_pid, STRESS_SYNC_START_FLAG_WAITING);
	if (kill(pid, SIGSTOP) < 0) {
		pr_inf("%s: cannot stop stressor on for --sync-start, errno=%d (%s)",
			args->name, errno, strerror(errno));
	}
	stress_sync_state_store(s_pid, STRESS_SYNC_START_FLAG_RUNNING);
	stress_sync_start_timeout();
}

/*
 *  stress_sync_start_cont_s_pid()
 *	wake up (continue) a stopped process
 */
void stress_sync_start_cont_s_pid(stress_pid_t *s_pid)
{
	pid_t pid;

	if (!(g_opt_flags & OPT_FLAGS_SYNC_START))
		return;

	pid = s_pid->oomable_child ? s_pid->oomable_child : s_pid->pid;
	if (pid <= 1)
		return;

	(void)kill(pid, SIGCONT);
}

void stress_sync_start_cont_list(stress_pid_t *s_pids_head)
{
	int unready, n_pids;

	if (!(g_opt_flags & OPT_FLAGS_SYNC_START))
		return;

	do {
		stress_pid_t *s_pid;

		unready = 0;
		n_pids = 0;
		for (s_pid = s_pids_head; s_pid; s_pid = s_pid->next) {
			uint8_t state;

			n_pids++;
			stress_sync_state_load(s_pid, &state);
			if (state == STRESS_SYNC_START_FLAG_FINISHED)
				continue;
			if (state != STRESS_SYNC_START_FLAG_WAITING)
				unready++;
		}
		if (!unready)
			break;
		(void)shim_usleep(10000);
		unready = 0;
	} while (stress_continue_flag());

	if (!unready) {
		do {
			stress_pid_t *s_pid;
			int running = 0, finished = 0;
			uint8_t state;

			for (s_pid = s_pids_head; s_pid; s_pid = s_pid->next) {
				stress_sync_start_cont_s_pid(s_pid);
				stress_sync_state_load(s_pid, &state);
				if (state == STRESS_SYNC_START_FLAG_FINISHED)
					finished++;
				if (state == STRESS_SYNC_START_FLAG_RUNNING)
					running++;
			}
			if ((running + finished) == n_pids)
				break;
			(void)shim_usleep(10000);
		} while (stress_continue_flag());
	}
}
