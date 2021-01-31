/*
 * Copyright (C) 2021 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

#if defined(__linux__)

static stress_vmstat_t vmstat_prev;
static uint64_t vmstat_count;
static pid_t vmstat_pid;

/*
 *  stress_next_field()
 *	skip to next field, returns false if end of
 *	string and/or no next field.
 */
static bool stress_next_field(char **str)
{
	char *ptr = *str;

	while (*ptr && *ptr != ' ')
		ptr++;

	if (!*ptr)
		return false;

	while (*ptr && *ptr == ' ')
		ptr++;

	if (!*ptr)
		return false;

	*str = ptr;
	return true;
}

/*
 *  stress_read_vmstat()
 *	read vmstat statistics
 */
static void stress_read_vmstat(stress_vmstat_t *vmstat)
{
	FILE *fp;
	char buffer[1024];

	(void)memset(vmstat, 0, sizeof(*vmstat));

	fp = fopen("/proc/stat", "r");
	while (fp && fgets(buffer, sizeof(buffer), fp)) {
		char *ptr = buffer;

		if (!strncmp(buffer, "cpu ", 4))
			continue;
		if (!strncmp(buffer, "cpu", 3)) {
			if (!stress_next_field(&ptr))
				continue;
			/* user time */
			vmstat->user_time += (uint64_t)atoll(ptr);
			if (!stress_next_field(&ptr))
				continue;

			/* user time nice */
			vmstat->user_time += (uint64_t)atoll(ptr);
			if (!stress_next_field(&ptr))
				continue;

			/* system time */
			vmstat->system_time += (uint64_t)atoll(ptr);
			if (!stress_next_field(&ptr))
				continue;

			/* idle time */
			vmstat->idle_time += (uint64_t)atoll(ptr);
			if (!stress_next_field(&ptr))
				continue;

			/* iowait time */
			vmstat->wait_time += (uint64_t)atoll(ptr);
			if (!stress_next_field(&ptr))
				continue;

			/* irq time, account in system time */
			vmstat->system_time += (uint64_t)atoll(ptr);
			if (!stress_next_field(&ptr))
				continue;

			/* soft time, account in system time */
			vmstat->system_time += (uint64_t)atoll(ptr);
			if (!stress_next_field(&ptr))
				continue;

			/* stolen time */
			vmstat->stolen_time += (uint64_t)atoll(ptr);
			if (!stress_next_field(&ptr))
				continue;

			/* guest time, add to stolen stats */
			vmstat->stolen_time += (uint64_t)atoll(ptr);
			if (!stress_next_field(&ptr))
				continue;

			/* guest_nice time, add to stolen stats */
			vmstat->stolen_time += (uint64_t)atoll(ptr);
			if (!stress_next_field(&ptr))
				continue;
		}

		if (!strncmp(buffer, "intr", 4)) {
			if (!stress_next_field(&ptr))
				continue;
			/* interrupts */
			vmstat->interrupt = (uint64_t)atoll(ptr);
		}
		if (!strncmp(buffer, "ctxt", 4)) {
			if (!stress_next_field(&ptr))
				continue;
			/* context switches */
			vmstat->context_switch = (uint64_t)atoll(ptr);
		}
		if (!strncmp(buffer, "procs_running", 13)) {
			if (!stress_next_field(&ptr))
				continue;
			/* context switches */
			vmstat->procs_running = (uint64_t)atoll(ptr);
		}
		if (!strncmp(buffer, "procs_blocked", 13)) {
			if (!stress_next_field(&ptr))
				continue;
			/* context switches */
			vmstat->procs_blocked = (uint64_t)atoll(ptr);
		}
		if (!strncmp(buffer, "swap", 4)) {
			if (!stress_next_field(&ptr))
				continue;
			/* swap in */
			vmstat->swap_in = (uint64_t)atoll(ptr);

			if (!stress_next_field(&ptr))
				continue;
			/* swap out */
			vmstat->swap_out = (uint64_t)atoll(ptr);
		}
	}
	(void)fclose(fp);

	fp = fopen("/proc/meminfo", "r");
	while (fp && fgets(buffer, sizeof(buffer), fp)) {
		char *ptr = buffer;

		if (!strncmp(buffer, "MemFree", 7)) {
			if (!stress_next_field(&ptr))
				continue;
			vmstat->memory_free = (uint64_t)atoll(ptr);
		}
		if (!strncmp(buffer, "Buffers", 7)) {
			if (!stress_next_field(&ptr))
				continue;
			vmstat->memory_buff = (uint64_t)atoll(ptr);
		}
		if (!strncmp(buffer, "Cached", 6)) {
			if (!stress_next_field(&ptr))
				continue;
			vmstat->memory_cache = (uint64_t)atoll(ptr);
		}
		if (!strncmp(buffer, "SwapTotal", 9)) {
			if (!stress_next_field(&ptr))
				continue;
			vmstat->swap_total = (uint64_t)atoll(ptr);
		}
		if (!strncmp(buffer, "SwapUsed", 8)) {
			if (!stress_next_field(&ptr))
				continue;
			vmstat->swap_used = (uint64_t)atoll(ptr);
		}
	}
	(void)fclose(fp);

	fp = fopen("/proc/vmstat", "r");
	while (fp && fgets(buffer, sizeof(buffer), fp)) {
		char *ptr = buffer;

		if (!strncmp(buffer, "pgpgin", 6)) {
			if (!stress_next_field(&ptr))
				continue;
			vmstat->block_in = (uint64_t)atoll(ptr);
		}
		if (!strncmp(buffer, "pgpgout", 7)) {
			if (!stress_next_field(&ptr))
				continue;
			vmstat->block_out = (uint64_t)atoll(ptr);
		}
		if (!strncmp(buffer, "pswpin", 6)) {
			if (!stress_next_field(&ptr))
				continue;
			vmstat->swap_in = (uint64_t)atoll(ptr);
		}
		if (!strncmp(buffer, "pswpout", 7)) {
			if (!stress_next_field(&ptr))
				continue;
			vmstat->swap_out = (uint64_t)atoll(ptr);
		}
	}
	(void)fclose(fp);
}

#define STRESS_VMSTAT_COPY(field)	vmstat->field = vmstat_current.field
#define STRESS_VMSTAT_DELTA(field)	vmstat->field = vmstat_current.field - vmstat_prev.field

/*
 *  stress_get_vmstat()
 *	collect vmstat data, zero for initial read
 */
static void stress_get_vmstat(stress_vmstat_t *vmstat)
{
	stress_vmstat_t vmstat_current;

	stress_read_vmstat(&vmstat_current);

	if (vmstat_count == 0) {
		(void)memset(vmstat, 0, sizeof(*vmstat));
	} else {
		STRESS_VMSTAT_COPY(procs_running);
		STRESS_VMSTAT_COPY(procs_blocked);
		STRESS_VMSTAT_COPY(swap_total);
		STRESS_VMSTAT_COPY(swap_used);
		STRESS_VMSTAT_COPY(memory_free);
		STRESS_VMSTAT_COPY(memory_buff);
		STRESS_VMSTAT_COPY(memory_cache);
		STRESS_VMSTAT_DELTA(swap_in);
		STRESS_VMSTAT_DELTA(swap_out);
		STRESS_VMSTAT_DELTA(block_in);
		STRESS_VMSTAT_DELTA(block_out);
		STRESS_VMSTAT_DELTA(interrupt);
		STRESS_VMSTAT_DELTA(context_switch);
		STRESS_VMSTAT_DELTA(user_time);
		STRESS_VMSTAT_DELTA(system_time);
		STRESS_VMSTAT_DELTA(idle_time);
		STRESS_VMSTAT_DELTA(wait_time);
		STRESS_VMSTAT_DELTA(stolen_time);
	}
	vmstat_count++;

	(void)memcpy(&vmstat_prev, &vmstat_current, sizeof(vmstat_prev));
}

/*
 *  stress_vmstat_start()
 *	start vmstat statistics (1 per second)
 */
void stress_vmstat_start(void)
{
	vmstat_pid = fork();
	if (vmstat_pid < 0)
		return;
	if (vmstat_pid == 0) {
		stress_vmstat_t vmstat;

		stress_get_vmstat(&vmstat);
		for (;;) {
			unsigned long clk_tick;

			sleep(1);
			if ((vmstat_count % 60) == 1)
				pr_inf("vmstat %2s %2s %9s %9s %9s %9s "
					"%4s %4s %6s %6s %4s %4s %2s %2s "
					"%2s %2s %2s\n",
					"r", "b", "swpd", "free", "buff",
					"cache", "si", "so", "bi", "bo",
					"in", "cs", "us", "sy", "id",
					"wa", "st");

			stress_get_vmstat(&vmstat);
			clk_tick = sysconf(_SC_CLK_TCK) * sysconf(_SC_NPROCESSORS_ONLN);
			pr_inf("vmstat %2" PRIu64 " %2" PRIu64 /* procs */
			       " %9" PRIu64 " %9" PRIu64	/* vm used */
			       " %9" PRIu64 " %9" PRIu64	/* memory_buff */
			       " %4" PRIu64 " %4" PRIu64	/* si, so*/
			       " %6" PRIu64 " %6" PRIu64	/* bi, bo*/
			       " %4" PRIu64 " %4" PRIu64	/* int, cs*/
			       " %2.0f %2.0f" 			/* us, sy */
			       " %2.0f %2.0f" 			/* id, wa */
			       " %2.0f\n",			/* st */
				vmstat.procs_running,
				vmstat.procs_blocked,
				vmstat.swap_total - vmstat.swap_used,
				vmstat.memory_free,
				vmstat.memory_buff,
				vmstat.memory_cache,
				vmstat.swap_in,
				vmstat.swap_out,
				vmstat.block_in,
				vmstat.block_out,
				vmstat.interrupt,
				vmstat.context_switch,
				100.0 * vmstat.user_time / clk_tick,
				100.0 * vmstat.system_time / clk_tick,
				100.0 * vmstat.idle_time / clk_tick,
				100.0 * vmstat.wait_time / clk_tick,
				100.0 * vmstat.stolen_time / clk_tick);
		}
	}
}

/*
 *  stress_vmstat_stop()
 *	stop vmstat statistics
 */
void stress_vmstat_stop(void)
{
	if (vmstat_pid > 0) {
		int status;

		(void)kill(vmstat_pid, SIGKILL);
		(void)waitpid(vmstat_pid, &status, 0);
	}
}

#else

/*
 *  no-ops for non-linux cases
 */
void stress_vmstat_start(void)
{
}

void stress_vmstat_stop(void)
{
}

#endif
