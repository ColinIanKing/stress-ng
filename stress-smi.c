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

static const stress_help_t help[] = {
	{ NULL,	"smi N",	"start N workers that trigger SMIs" },
	{ NULL,	"smi-ops N",	"stop after N SMIs have been triggered" },
	{ NULL,	NULL,		NULL }
};

#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_LIBKMOD_H) &&  \
    defined(HAVE_LIB_KMOD) &&   \
    defined(__linux__)

#define MSR_SMI_COUNT   (0x00000034)
#define APM_PORT	(0xb2)
#define SMI_LOOPS	(1000)

/*
 *  stress_smi_zero_regs
 *	inline helper to clear a..d
 */
static inline void stress_smi_zero_regs(
	uint32_t *a,
	uint32_t *b,
	uint32_t *c,
	uint32_t *d)
{
	*a = 0;
	*b = 0;
	*c = 0;
	*d = 0;
}

/*
 *  stress_smi_cpu_has_msr()
 *	return non-zero if CPU has MSR support
 */
static inline int stress_smi_cpu_has_msr(void)
{
	uint32_t a, b, c, d;

	stress_smi_zero_regs(&a, &b, &c, &d);
	__get_cpuid(1, &a, &b, &c, &d);
	return d & (1 << 5);

}

/*
 *  cpu_has_tsc()
 *
 */
static inline int cpu_has_tsc(void)
{
	uint32_t a, b, c, d;

	stress_smi_zero_regs(&a, &b, &c, &d);
	__get_cpuid(1, &a, &b, &c, &d);
	return d & (1 << 4);
}

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
        if (!stress_check_capability(CAP_SYS_RAWIO)) {
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
	if (!stress_smi_cpu_has_msr()) {
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
	int ret;

	*val = ~0;
	snprintf(buffer, sizeof(buffer), "/dev/cpu/%d/msr", cpu);
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
	int ret, rc = EXIT_SUCCESS;
	bool already_loaded = false;
	bool read_msr_ok = true;
	uint64_t s1 = 0, s2 = 0, smis;
	double d1 = 0.0, d2 = 0.0, secs, rate, duration;
	const int cpus = stress_get_processors_online();

	if (args->instance == 0) {
		ret = stress_module_load(args->name, "msr", NULL, &already_loaded);
		if (ret < 0)
			return EXIT_NO_RESOURCE;
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
		register int i;

		for (i = 0; i < SMI_LOOPS; i++) {
			outb(1, 0xb2);
		}
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	ret = ioperm(APM_PORT, 2, 0);
	(void)ret;

	if (args->instance == 0) {
		d2 = stress_time_now();
		if (stress_smi_count(cpus, &s2) < 0)
			read_msr_ok = false;

		if (read_msr_ok) {
			secs = d2 - d1;
			smis = (s2 - s1) / cpus;
			rate = (secs > 0.0) ? (double)smis / secs : 0.0;
			duration = (rate > 0.0) ? 1000000.0 / rate : 0.0;

			if ((secs > 0.0) && (duration > 0.0)) {
				pr_inf("%s: %.2f SMIs per second per CPU (%.2f us per SMI)\n",
					args->name, rate, duration);
			} else {
				pr_inf("%s: cannot determine SMI rate, data is not unreliable\n",
					args->name);
			}
		} else {
			pr_inf("%s: cannot determine SMI rate, MSI_SMI_COUNT not readable\n",
				args->name);
		}

		ret = stress_module_unload(args->name, "msr", already_loaded);
		(void)ret;
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
