// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-arch.h"

#define MSR_SMI_COUNT		(0x34)

typedef void (*pr_func_t)(const char *fmt, ...);

typedef struct {
	const char *type;		/* /proc/interrupts interrupt name */
	const bool check_failure;	/* check interrupt delta, flag as failure if > 0 */
	const pr_func_t pr_func FORMAT(printf, 1, 2); /* logging function to use */
	const char *descr;		/* description of interrupt */
} stress_interrupt_info_t;

static const stress_interrupt_info_t info[] = {
	{ "MCE:",	true,	pr_fail, "Machine Check Exception" },
	{ "TRM:",	false,	pr_inf,  "Thermal Event Interrupt" },
	{ "SPU:",	false,	pr_warn, "Spurious Interrupt" },
#if defined(STRESS_ARCH_X86)
	{ "DFR:",	true,	pr_fail, "Deferred Error APIC interrupt" },
	{ "ERR:",	true,	pr_fail, "IO-APIC Bus Error" },
	{ "SMI:",	false,	pr_warn, "System Management Interrupt" },
	{ "MIS:",	true,	pr_fail, "IO-APIC Miscount" },
#endif
	{ "Err:",	true,	pr_fail, "Spurious Unhandled Interrupt" },	/* ARM */
};

STRESS_ASSERT(SIZEOF_ARRAY(info) <= STRESS_INTERRUPTS_MAX)

/*
 *  stress_interrupts_count_by_type()
 *	count up all interrupts of given named type
 */
static uint64_t stress_interrupts_count_by_type(const char *type)
{
	FILE *fp;
	char buffer[4096];
	uint64_t count = 0;

	/*
	 *  Get SMI count, x86 only AND when run as root AND smi driver is installed
	 */
	if (!strncmp("SMI:", type, 4)) {
#if defined(STRESS_ARCH_X86)
		unsigned int cpu;

		if ((shim_getcpu(&cpu, NULL, NULL) == 0) &&
		    (stress_x86_smi_readmsr64(cpu, MSR_SMI_COUNT, &count) == 0))
			return count;
#endif
		return 0;
	}

	fp = fopen("/proc/interrupts", "r");
	if (!fp)
		return 0;

	while (fgets(buffer, sizeof(buffer), fp)) {
		char *ptr;

		/* Find a match */
		ptr = strstr(buffer, type);
		if (ptr) {
			ptr += strlen(type);
			for (;;) {
				uint64_t val = 0ULL;

				/* skip spaces */
				while (*ptr == ' ')
					ptr++;
				if (!*ptr)
					break;

				/* expecting number, bail otherwise */
				if (!isdigit((int)*ptr))
					break;

				/* get count, sum it */
				if (sscanf(ptr, "%" SCNu64, &val) == 1)
					count += val;

				/* scan over digits */
				while (isdigit((int)*ptr))
					ptr++;

				/* bail if end of string */
				if (!*ptr)
					break;
			}
			break;
		}
	}
	(void)fclose(fp);

	return count;
}

/*
 *  stress_interrupts_start()
 *	count interrupts at start of run
 */
void stress_interrupts_start(stress_interrupts_t *counters)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(info); i++) {
		counters[i].count_start = stress_interrupts_count_by_type(info[i].type);
		counters[i].count_stop = counters[i].count_start;
	}
}

/*
 *  stress_interrupts_start()
 *	count interrupts at stop of run
 */
void stress_interrupts_stop(stress_interrupts_t *counters)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(info); i++)
		counters[i].count_stop = stress_interrupts_count_by_type(info[i].type);
}

/*
 *  stress_interrupts_check_failure()
 *	set rc to EXIT_FAILURE and report a failure for failure
 *	specific interrupts (e.g. MCE machine check error interrupts)
 */
void stress_interrupts_check_failure(const char *name, stress_interrupts_t *counters, uint32_t instance, int *rc)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(info); i++) {
		if (info[i].check_failure) {
			const int64_t delta = counters[i].count_stop - counters[i].count_start;

			if (delta > 0) {
				if (instance == 0) {
					const char *plural = delta > 1 ? "s" : "";
					pr_fail("%s: detected at least %" PRId64 " %s%s\n", name, delta, info[i].descr, plural);
				}
				*rc = EXIT_FAILURE;
			}
		}
	}

}

/*
 *  stress_interrupt_tolower()
 *	yaml-ise interrupt description string, replace
 *	spaces with _ and force to lower case
 */
static inline void stress_interrupt_tolower(char *str)
{
	while (*str) {
		*str = (*str == ' ') ? '_' : tolower((int)*str);
		str++;
	}
}

/*
 *  stress_interrupts_dump()
 *	dump interrupts report
 */
void stress_interrupts_dump(FILE *yaml, stress_stressor_t *stressors_list)
{
	stress_stressor_t *ss;
	size_t i;
	bool pr_heading = false;

	for (ss = stressors_list; ss; ss = ss->next) {
		bool pr_nl = false;
		bool pr_name = false;

		if (ss->ignore.run)
			continue;

		for (i = 0; i < SIZEOF_ARRAY(info); i++) {
			uint64_t total = 0;
			int count = 0;
			int32_t j;

			for (j = 0; j < ss->num_instances; j++) {
				const int64_t delta =
					ss->stats[j]->interrupts[i].count_stop -
					ss->stats[j]->interrupts[i].count_start;
				if (delta > 0) {
					total += delta;
					count++;
				}
			}

			if ((total > 0) && (count > 0)) {
				char munged[64];
				const double average = round((double)total / (double)count);
				const char *plural = average > 1.0 ? "s" : "";

				if (!pr_heading) {
					pr_yaml(yaml, "interrupts:\n");
					pr_heading = true;
				}

				if (!pr_name) {
					(void)stress_munge_underscore(munged, ss->stressor->name, sizeof(munged));
					pr_inf("%s:\n", munged);
					pr_yaml(yaml, "    - stressor: %s\n", munged);
					pr_name = true;
				}

				info[i].pr_func("   %7.0f %s%s%s\n", average, info[i].descr, plural,
					info[i].check_failure ? " (Failure)" : "");
				(void)shim_strlcpy(munged, info[i].descr, sizeof(munged));
				stress_interrupt_tolower(munged);
				pr_yaml(yaml, "      %s%s: %.0f\n", munged, plural, average);
				pr_nl = true;
			}
		}
		if (pr_nl)
			pr_yaml(yaml, "\n");
	}
}
