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

int32_t vmstat_delay = 0;
int32_t thermalstat_delay = 0;

int stress_set_vmstat(const char *const opt)
{
	vmstat_delay = stress_get_int32(opt);
        if ((vmstat_delay < 1) || (vmstat_delay > 3600)) {
                (void)fprintf(stderr, "vmstat must in the range 1 to 3600.\n");
                _exit(EXIT_FAILURE);
        }
	return 0;
}

int stress_set_thermalstat(const char *const opt)
{
	thermalstat_delay = stress_get_int32(opt);
        if ((thermalstat_delay < 1) || (thermalstat_delay > 3600)) {
                (void)fprintf(stderr, "thermalstat must in the range 1 to 3600.\n");
                _exit(EXIT_FAILURE);
        }
	return 0;
}

#if defined(__linux__)

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

	while (*ptr == ' ')
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
	if (fp) {
		while (fgets(buffer, sizeof(buffer), fp)) {
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
	}

	fp = fopen("/proc/meminfo", "r");
	if (fp) {
		while (fgets(buffer, sizeof(buffer), fp)) {
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
	}

	fp = fopen("/proc/vmstat", "r");
	if (fp) {
		while (fgets(buffer, sizeof(buffer), fp)) {
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
}

#define STRESS_VMSTAT_COPY(field)	vmstat->field = (vmstat_current.field)
#define STRESS_VMSTAT_DELTA(field)					\
	vmstat->field = ((vmstat_current.field > vmstat_prev.field) ?	\
	(vmstat_current.field - vmstat_prev.field) : 0)

/*
 *  stress_get_vmstat()
 *	collect vmstat data, zero for initial read
 */
static void stress_get_vmstat(stress_vmstat_t *vmstat)
{
	static stress_vmstat_t vmstat_prev;
	stress_vmstat_t vmstat_current;

	stress_read_vmstat(&vmstat_current);
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
	(void)memcpy(&vmstat_prev, &vmstat_current, sizeof(vmstat_prev));
}

/*
 *  stress_get_tz_info()
 *	get temperature in degrees C from a thermal zone
 */
static double stress_get_tz_info(stress_tz_info_t *tz_info)
{
	double temp = 0.0;
	FILE *fp;
	char path[PATH_MAX];

	(void)snprintf(path, sizeof(path),
		"/sys/class/thermal/%s/temp",
		tz_info->path);

	if ((fp = fopen(path, "r")) != NULL) {
		if (fscanf(fp, "%lf", &temp) == 1)
			temp /= 1000.0;
		(void)fclose(fp);
	}
	return temp;
}

/*
 *  stress_get_cpu_ghz_average()
 *	compute average CPU frequencies in GHz
 */
static double stress_get_cpu_ghz_average(void)
{
	struct dirent **cpu_list = NULL;
	int i, n_cpus, n = 0;
	double total_freq = 0.0;

	n_cpus = scandir("/sys/devices/system/cpu", &cpu_list, NULL, alphasort);
	for (i = 0; i < n_cpus; i++) {
		char *name = cpu_list[i]->d_name;

		if (!strncmp(name, "cpu", 3) && isdigit(name[3])) {
			char path[PATH_MAX];
			double freq;
			FILE *fp;

			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/%s/cpufreq/scaling_cur_freq",
				name);
			if ((fp = fopen(path, "r")) != NULL) {
				if (fscanf(fp, "%lf", &freq) == 1) {
					total_freq += freq;
					n++;
				}
				(void)fclose(fp);
			}
		}
		free(cpu_list[i]);
	}
	if (n_cpus > -1)
		free(cpu_list);

	return (n == 0) ? 0.0 : (total_freq / n) / 1000000.0;
}

/*
 *  stress_vmstat_start()
 *	start vmstat statistics (1 per second)
 */
void stress_vmstat_start(void)
{
	stress_vmstat_t vmstat;
	size_t tz_num = 0;
	stress_tz_info_t *tz_info, *tz_info_list;
	int32_t vmstat_sleep, thermalstat_sleep;

	if ((vmstat_delay == 0) && (thermalstat_delay == 0))
		return;

	vmstat_sleep = vmstat_delay;
	thermalstat_sleep = thermalstat_delay;

	vmstat_pid = fork();
	if (vmstat_pid < 0 || vmstat_pid > 0)
		return;

	if (vmstat_delay)
		stress_get_vmstat(&vmstat);

	if (thermalstat_delay) {
		tz_info_list = NULL;
		stress_tz_init(&tz_info_list);

		for (tz_info = tz_info_list; tz_info; tz_info = tz_info->next)
			tz_num++;
	}

	while (keep_stressing_flag()) {
		int32_t sleep_delay = 1;

		if ((vmstat_sleep > 0) && (thermalstat_sleep > 0))
			sleep_delay = STRESS_MINIMUM(vmstat_sleep, thermalstat_sleep);
		else if ((vmstat_sleep > 0) && (thermalstat_sleep == 0))
			sleep_delay = vmstat_sleep;
		else if ((thermalstat_sleep > 0) && (vmstat_sleep == 0))
			sleep_delay = thermalstat_sleep;

		sleep(sleep_delay);

		vmstat_sleep -= sleep_delay;
		thermalstat_sleep -= sleep_delay;

		if ((vmstat_delay > 0) && (vmstat_sleep <= 0))
			vmstat_sleep = vmstat_delay;
		if ((thermalstat_delay > 0) && (thermalstat_sleep <= 0))
			thermalstat_sleep = thermalstat_delay;

		if (vmstat_sleep == vmstat_delay) {
			unsigned long clk_tick;
			static uint32_t vmstat_count = 0;

			if ((vmstat_count++ % 25) == 0)
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
				vmstat.swap_in / vmstat_delay,
				vmstat.swap_out / vmstat_delay,
				vmstat.block_in / vmstat_delay,
				vmstat.block_out / vmstat_delay,
				vmstat.interrupt / vmstat_delay,
				vmstat.context_switch / vmstat_delay,
				100.0 * vmstat.user_time / (clk_tick * vmstat_delay),
				100.0 * vmstat.system_time / (clk_tick * vmstat_delay),
				100.0 * vmstat.idle_time / (clk_tick * vmstat_delay),
				100.0 * vmstat.wait_time / (clk_tick * vmstat_delay),
				100.0 * vmstat.stolen_time / (clk_tick * vmstat_delay));
		}

		if (thermalstat_delay == thermalstat_sleep) {
			double min1, min5, min15, ghz;
			char therms[1 + (tz_num * 6)];
			char cpuspeed[6];
			char *ptr;
			static uint32_t thermalstat_count = 0;

			(void)memset(therms, 0, sizeof(therms));

			for (ptr = therms, tz_info = tz_info_list; tz_info; tz_info = tz_info->next) {
				(void)snprintf(ptr, 8, " %6.6s", tz_info->type);
				ptr += 7;
			}

			if ((thermalstat_count++ % 60) == 0)
				pr_inf("therm:   GHz  LdA1  LdA5 LdA15 %s\n", therms);

			for (ptr = therms, tz_info = tz_info_list; tz_info; tz_info = tz_info->next) {
				(void)snprintf(ptr, 8, " %6.2f", stress_get_tz_info(tz_info));
				ptr += 7;
			}
			ghz = stress_get_cpu_ghz_average();
			if (ghz > 0.0)
				(void)snprintf(cpuspeed, sizeof(cpuspeed), "%5.2f", ghz);
			else
				(void)shim_strlcpy(cpuspeed, "n/a", sizeof(cpuspeed));

			if (stress_get_load_avg(&min1, &min5, &min15) < 0)  {
				pr_inf("therm: %5s %5.5s %5.5s %5.5s %s\n",
					cpuspeed, "n/a", "n/a", "n/a", therms);
			} else {
				pr_inf("therm: %5s %5.2f %5.2f %5.2f %s\n",
					cpuspeed, min1, min5, min15, therms);
			}
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
