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
#include "core-cpu-freq.h"

#include <ctype.h>
#include <float.h>

#if defined(HAVE_SYS_SYSCTL_H) &&	\
    !defined(__linux__)
#include <sys/sysctl.h>
#endif

/*
 *  stress_zero_cpu_ghz()
 *	zero CPU clock freq stats
 */
static inline void stress_zero_cpu_ghz(
	double *avg_ghz,
	double *min_ghz,
	double *max_ghz)
{
	*avg_ghz = 0.0;
	*min_ghz = 0.0;
	*max_ghz = 0.0;
}

#if defined(__linux__)
/*
 *  stress_get_cpu_freq()
 *	get CPU frequencies in GHz
 */
void stress_get_cpu_freq(
	double *avg_ghz,
	double *min_ghz,
	double *max_ghz)
{
	struct dirent **cpu_list = NULL;
	int i, n_cpus, n = 0;
	double total_freq = 0.0;

	*min_ghz = DBL_MAX;
	*max_ghz = 0.0;

	n_cpus = scandir("/sys/devices/system/cpu", &cpu_list, NULL, alphasort);
	for (i = 0; i < n_cpus; i++) {
		const char *name = cpu_list[i]->d_name;

		if (!strncmp(name, "cpu", 3) && isdigit((unsigned char)name[3])) {
			char path[PATH_MAX];
			double freq;
			FILE *fp;

			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/%s/cpufreq/scaling_cur_freq",
				name);
			if ((fp = fopen(path, "r")) != NULL) {
				if (fscanf(fp, "%lf", &freq) == 1) {
					if (freq >= 0.0) {
						total_freq += freq;
						if (*min_ghz > freq)
							*min_ghz = freq;
						if (*max_ghz < freq)
							*max_ghz = freq;
						n++;
					}
				}
				(void)fclose(fp);
			}
		}
		free(cpu_list[i]);
	}
	if (n_cpus > -1)
		free(cpu_list);

	if (n == 0) {
		stress_zero_cpu_ghz(avg_ghz, min_ghz, max_ghz);
	} else {
		*avg_ghz = (total_freq / n) * ONE_MILLIONTH;
		*min_ghz *= ONE_MILLIONTH;
		*max_ghz *= ONE_MILLIONTH;
	}
}
#elif defined(__FreeBSD__) ||	\
      defined(__APPLE__)
void stress_get_cpu_freq(
	double *avg_ghz,
	double *min_ghz,
	double *max_ghz)
{
	const int32_t ncpus = stress_get_processors_configured();
	int32_t i;
	double total_freq = 0.0;
	int n = 0;

	*min_ghz = DBL_MAX;
	*max_ghz = 0.0;

	for (i = 0; i < ncpus; i++) {
		double freq;
#if defined(__FreeBSD__)
		{
			char name[32];

			(void)snprintf(name, sizeof(name), "dev.cpu.%" PRIi32 ".freq", i);
			freq = (double)stress_bsd_getsysctl_uint(name) * ONE_THOUSANDTH;
		}
#elif defined(__APPLE__)
		freq = (double)stress_bsd_getsysctl_uint64("hw.cpufrequency") * ONE_BILLIONTH;
#endif
		if (freq >= 0.0) {
			total_freq += freq;
			if (*min_ghz > freq)
				*min_ghz = freq;
			if (*max_ghz < freq)
				*max_ghz = freq;
			n++;
		}
	}
	if (n == 0) {
		stress_zero_cpu_ghz(avg_ghz, min_ghz, max_ghz);
	} else {
		*avg_ghz = (total_freq / n);
	}
}
#elif defined(__OpenBSD__)
void stress_get_cpu_freq(
	double *avg_ghz,
	double *min_ghz,
	double *max_ghz)
{
	int mib[2], speed_mhz;
	size_t size;

	mib[0] = CTL_HW;
	mib[1] = HW_CPUSPEED;
	size = sizeof(speed_mhz);
	if (sysctl(mib, 2, &speed_mhz, &size, NULL, 0) == 0) {
		*avg_ghz = (double)speed_mhz / 1000.0;
		*min_ghz = *avg_ghz;
		*max_ghz = *avg_ghz;
	} else {
		stress_zero_cpu_ghz(avg_ghz, min_ghz, max_ghz);
	}
}
#else
void stress_get_cpu_freq(
	double *avg_ghz,
	double *min_ghz,
	double *max_ghz)
{
	stress_zero_cpu_ghz(avg_ghz, min_ghz, max_ghz);
}
#endif
