/*
 * Copyright (C) 2021 Canonical, Ltd.
 * Copyright (C) 2022 Colin Ian King.
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
#include "core-arch.h"
#include "core-cpu.h"
#include "core-capabilities.h"

#if defined(HAVE_SYS_IO_H)
#include <sys/io.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"smi N",	"start N workers that trigger SMIs" },
	{ NULL,	"smi-ops N",	"stop after N SMIs have been triggered" },
	{ NULL,	NULL,		NULL }
};

#if defined(STRESS_ARCH_X86) &&		\
    defined(HAVE_IOPORT) &&		\
    defined(HAVE_SYS_IO_H) &&		\
    defined(__linux__)

#define MSR_SMI_COUNT   (0x00000034)
#define APM_PORT	(0xb2)
#define STRESS_SMI_NOP	(0x90)	/* SMI No-op command */

/*
 *  stress_smi_supported()
 *      check if we can run this with SHIM_CAP_SYS_MODULE capability
 */
static int stress_smi_supported(const char *name)
{
        if (!stress_check_capability(SHIM_CAP_SYS_MODULE)) {
                pr_inf_skip("%s stressor will be skipped, "
                        "need to be running with CAP_SYS_MODULE "
                        "rights for this stressor\n", name);
                return -1;
        }
        if (!stress_check_capability(SHIM_CAP_SYS_RAWIO)) {
                pr_inf_skip("%s stressor will be skipped, "
                        "need to be running with CAP_SYS_RAWIO "
                        "rights for this stressor\n", name);
                return -1;
        }
	if (!stress_check_capability(SHIM_CAP_IS_ROOT)) {
                pr_inf_skip("%s stressor will be skipped, "
                        "need to be running with root"
                        "rights for this stressor\n", name);
                return -1;
	}
	if (!stress_cpu_x86_has_msr()) {
                pr_inf_skip("%s stressor will be skipped, "
                        "CPU cannot read model specific registers (MSR)\n",
                        name);
	}
        return 0;
}

/*
 *  stress_smi_readmsr()
 *	64 bit read an MSR on a specified CPU
 */
static int stress_smi_readmsr64(const int cpu, const uint32_t reg, uint64_t *val)
{
	char buffer[PATH_MAX];
	uint64_t value = 0;
	int fd;
	ssize_t ret;

	*val = ~0UL;
	(void)snprintf(buffer, sizeof(buffer), "/dev/cpu/%d/msr", cpu);
	if ((fd = open(buffer, O_RDONLY)) < 0)
		return -1;

	ret = pread(fd, &value, 8, reg);
	(void)close(fd);
	if (ret < 0)
		return -1;

	*val = value;
	return 0;
}

/*
 *  stress_smi_count()
 *	read SMI count across all CPUs, return -1 if not readable
 */
static int stress_smi_count(const int cpus, uint64_t *count)
{
	register int i;

	*count = 0;

	for (i = 0; i < cpus; i++) {
		uint64_t val;
		int ret;

		ret = stress_smi_readmsr64(i, MSR_SMI_COUNT, &val);
		if (ret < 0)
			return -1;
		*count += val;
	}
	return 0;
}

/*
 *  stress_smi()
 *	stress x86 systems by triggering SMIs
 */
static int stress_smi(const stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
	bool already_loaded = false;
	bool read_msr_ok = true;
	bool load_module = false;
	uint64_t s1 = 0, val;
	double d1 = 0.0;
	const int cpus = stress_get_processors_online();

	/*
	 *  If MSR can't be read maybe we need to load
	 *  the module to do so
	 */
	if (stress_smi_readmsr64(0, MSR_SMI_COUNT, &val) < 0)
		load_module = true;

	/*
	 *  Module load failure is not a problem, it just means
	 *  we can't get the SMI count
	 */
	if (load_module && (args->instance == 0)) {
		VOID_RET(int, stress_module_load(args->name, "msr", NULL, &already_loaded));
	}

	if (ioperm(APM_PORT, 2, 1) < 0) {
		pr_inf("%s: stressor will be skipped, cannot enable write "
			"permissions on the APM port 0x%2x\n",
			args->name, APM_PORT);
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (args->instance == 0) {
		d1 = stress_time_now();
		if (stress_smi_count(cpus, &s1) < 0)
			read_msr_ok = false;
	}

	do {
		outb(STRESS_SMI_NOP, APM_PORT);
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	VOID_RET(int, ioperm(APM_PORT, 2, 0));

	if (args->instance == 0) {
		uint64_t s2;
		const double d2 = stress_time_now();

		if (stress_smi_count(cpus, &s2) < 0)
			read_msr_ok = false;

		if (read_msr_ok) {
			const double secs = d2 - d1;
			const uint64_t smis = (s2 - s1) / (uint64_t)cpus;
			const double rate = (secs > 0.0) ? (double)smis / secs : 0.0;
			const double duration = (rate > 0.0) ? 1000000.0 / rate : 0.0;

			if ((secs > 0.0) && (duration > 0.0)) {
				pr_inf("%s: %.2f SMIs per second per CPU (%.2fus per SMI)\n",
					args->name, rate, duration);
			} else {
				pr_inf("%s: cannot determine SMI rate, data is not unreliable\n",
					args->name);
			}
		} else {
			pr_inf("%s: cannot determine SMI rate, MSR_SMI_COUNT not readable\n",
				args->name);
		}

		if (load_module) {
			VOID_RET(int, stress_module_unload(args->name, "msr", already_loaded));
		}
	}

	return rc;
}

stressor_info_t stress_smi_info = {
	.stressor = stress_smi,
	.class = CLASS_CPU | CLASS_PATHOLOGICAL,
	.help = help,
	.supported = stress_smi_supported
};
#else
stressor_info_t stress_smi_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU | CLASS_PATHOLOGICAL,
	.help = help,
};
#endif
