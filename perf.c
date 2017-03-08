/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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
#include "perf-event.h"

#if defined(STRESS_PERF_STATS)
/* perf enabled systems */

#include <locale.h>
#include <linux/perf_event.h>

#define THOUSAND	(1.0E3)
#define MILLION		(1.0E6)
#define BILLION		(1.0E9)
#define TRILLION	(1.0E12)
#define QUADRILLION	(1.0E15)
#define QUINTILLION	(1.0E18)

/* used for table of perf events to gather */
typedef struct {
	int id;				/* stress-ng perf ID */
	unsigned long type;		/* perf types */
	unsigned long config;		/* perf type specific config */
	char *label;			/* human readable name for perf type */
} perf_info_t;

/* perf data */
typedef struct {
	uint64_t counter;		/* perf counter */
	uint64_t time_enabled;		/* perf time enabled */
	uint64_t time_running;		/* perf time running */
} perf_data_t;

/* perf trace point id -> path resolution */
typedef struct {
	int id;				/* stress-ng perf ID */
	char *path;			/* path to config value */
} perf_tp_info_t;

#define PERF_TP_INFO(id, path) \
	{ STRESS_PERF_ ## id, path }

#define PERF_INFO(type, config, label)	\
	{ STRESS_PERF_ ## config, PERF_TYPE_ ## type, \
	  PERF_COUNT_ ## config, label }

#define STRESS_GOT(x) _SNG_PERF_COUNT_ ## x

#define UNRESOLVED				(~0UL)
#define PERF_COUNT_TP_SYSCALLS_ENTER		UNRESOLVED
#define PERF_COUNT_TP_SYSCALLS_EXIT		UNRESOLVED
#define PERF_COUNT_TP_TLB_FLUSH			UNRESOLVED
#define PERF_COUNT_TP_KMALLOC			UNRESOLVED
#define PERF_COUNT_TP_KMALLOC_NODE		UNRESOLVED
#define PERF_COUNT_TP_KFREE			UNRESOLVED
#define PERF_COUNT_TP_KMEM_CACHE_ALLOC		UNRESOLVED
#define PERF_COUNT_TP_KMEM_CACHE_ALLOC_NODE	UNRESOLVED
#define PERF_COUNT_TP_KMEM_CACHE_FREE		UNRESOLVED
#define PERF_COUNT_TP_MM_PAGE_ALLOC		UNRESOLVED
#define PERF_COUNT_TP_MM_PAGE_FREE		UNRESOLVED
#define PERF_COUNT_TP_RCU_UTILIZATION		UNRESOLVED
#define PERF_COUNT_TP_SCHED_MIGRATE_TASK	UNRESOLVED
#define PERF_COUNT_TP_SCHED_MOVE_NUMA		UNRESOLVED
#define PERF_COUNT_TP_SCHED_WAKEUP		UNRESOLVED
#define PERF_COUNT_TP_SIGNAL_GENERATE		UNRESOLVED
#define PERF_COUNT_TP_SIGNAL_DELIVER		UNRESOLVED
#define PERF_COUNT_TP_PAGE_FAULT_USER		UNRESOLVED
#define PERF_COUNT_TP_PAGE_FAULT_KERNEL		UNRESOLVED
#define PERF_COUNT_TP_IRQ_ENTRY			UNRESOLVED
#define PERF_COUNT_TP_IRQ_EXIT			UNRESOLVED
#define PERF_COUNT_TP_SOFTIRQ_ENTRY		UNRESOLVED
#define PERF_COUNT_TP_SOFTIRQ_EXIT		UNRESOLVED
#define PERF_COUNT_TP_RCU_UTILIZATION		UNRESOLVED
#define PERF_COUNT_TP_WRITEBACK_DIRTY_INODE	UNRESOLVED
#define PERF_COUNT_TP_WRITEBACK_DIRTY_PAGE	UNRESOLVED


/* perf counters to be read */
static perf_info_t perf_info[STRESS_PERF_MAX + 1] = {
#if STRESS_GOT(HW_CPU_CYCLES)
	PERF_INFO(HARDWARE, HW_CPU_CYCLES,		"CPU Cycles"),
#endif
#if STRESS_GOT(HW_INSTRUCTIONS)
	PERF_INFO(HARDWARE, HW_INSTRUCTIONS,		"Instructions"),
#endif
#if STRESS_GOT(HW_CACHE_REFERENCES)
	PERF_INFO(HARDWARE, HW_CACHE_REFERENCES,	"Cache References"),
#endif
#if STRESS_GOT(HW_CACHE_MISSES)
	PERF_INFO(HARDWARE, HW_CACHE_MISSES,		"Cache Misses"),
#endif
#if STRESS_GOT(HW_STALLED_CYCLES_FRONTEND)
	PERF_INFO(HARDWARE, HW_STALLED_CYCLES_FRONTEND,	"Stalled Cycles Frontend"),
#endif
#if STRESS_GOT(HW_STALLED_CYCLES_BACKEND)
	PERF_INFO(HARDWARE, HW_STALLED_CYCLES_BACKEND,	"Stalled Cycles Backend"),
#endif
#if STRESS_GOT(HW_BRANCH_INSTRUCTIONS)
	PERF_INFO(HARDWARE, HW_BRANCH_INSTRUCTIONS,	"Branch Instructions"),
#endif
#if STRESS_GOT(HW_BRANCH_MISSES)
	PERF_INFO(HARDWARE, HW_BRANCH_MISSES,		"Branch Misses"),
#endif
#if STRESS_GOT(HW_BUS_CYCLES)
	PERF_INFO(HARDWARE, HW_BUS_CYCLES,		"Bus Cycles"),
#endif
#if STRESS_GOT(HW_REF_CPU_CYCLES)
	PERF_INFO(HARDWARE, HW_REF_CPU_CYCLES,		"Total Cycles"),
#endif

#if STRESS_GOT(SW_PAGE_FAULTS_MIN)
	PERF_INFO(SOFTWARE, SW_PAGE_FAULTS_MIN,		"Page Faults Minor"),
#endif
#if STRESS_GOT(SW_PAGE_FAULTS_MAJ)
	PERF_INFO(SOFTWARE, SW_PAGE_FAULTS_MAJ,		"Page Faults Major"),
#endif
#if STRESS_GOT(SW_CONTEXT_SWITCHES)
	PERF_INFO(SOFTWARE, SW_CONTEXT_SWITCHES,	"Context Switches"),
#endif
#if STRESS_GOT(SW_CPU_MIGRATIONS)
	PERF_INFO(SOFTWARE, SW_CPU_MIGRATIONS,		"CPU Migrations"),
#endif
#if STRESS_GOT(SW_ALIGNMENT_FAULTS)
	PERF_INFO(SOFTWARE, SW_ALIGNMENT_FAULTS,	"Alignment Faults"),
#endif

	PERF_INFO(TRACEPOINT, TP_PAGE_FAULT_USER,	"Page Faults User"),
	PERF_INFO(TRACEPOINT, TP_PAGE_FAULT_KERNEL,	"Page Faults Kernel"),
	PERF_INFO(TRACEPOINT, TP_SYSCALLS_ENTER,	"System Call Enter"),
	PERF_INFO(TRACEPOINT, TP_SYSCALLS_EXIT,		"System Call Exit"),
	PERF_INFO(TRACEPOINT, TP_TLB_FLUSH,		"TLB Flushes"),
	PERF_INFO(TRACEPOINT, TP_KMALLOC,		"Kmalloc"),
	PERF_INFO(TRACEPOINT, TP_KMALLOC_NODE,		"Kmalloc Node"),
	PERF_INFO(TRACEPOINT, TP_KFREE,			"Kfree"),
	PERF_INFO(TRACEPOINT, TP_KMEM_CACHE_ALLOC,	"Kmem Cache Alloc"),
	PERF_INFO(TRACEPOINT, TP_KMEM_CACHE_ALLOC_NODE,	"Kmem Cache Alloc Node"),
	PERF_INFO(TRACEPOINT, TP_KMEM_CACHE_FREE,	"Kmem Cache Free"),
	PERF_INFO(TRACEPOINT, TP_MM_PAGE_ALLOC,		"MM Page Alloc"),
	PERF_INFO(TRACEPOINT, TP_MM_PAGE_FREE,		"MM Page Free"),
	PERF_INFO(TRACEPOINT, TP_RCU_UTILIZATION,	"RCU Utilization"),
	PERF_INFO(TRACEPOINT, TP_SCHED_MIGRATE_TASK,	"Sched Migrate Task"),
	PERF_INFO(TRACEPOINT, TP_SCHED_MOVE_NUMA,	"Sched Move NUMA"),
	PERF_INFO(TRACEPOINT, TP_SCHED_WAKEUP,		"Sched Wakeup"),
	PERF_INFO(TRACEPOINT, TP_SIGNAL_GENERATE,	"Signal Generate"),
	PERF_INFO(TRACEPOINT, TP_SIGNAL_DELIVER,	"Signal Deliver"),
	PERF_INFO(TRACEPOINT, TP_IRQ_ENTRY,		"IRQ Entry"),
	PERF_INFO(TRACEPOINT, TP_IRQ_EXIT,		"IRQ Exit"),
	PERF_INFO(TRACEPOINT, TP_SOFTIRQ_ENTRY,		"Soft IRQ Entry"),
	PERF_INFO(TRACEPOINT, TP_SOFTIRQ_EXIT,		"Soft IRQ Exit"),
	PERF_INFO(TRACEPOINT, TP_WRITEBACK_DIRTY_INODE,	"Writeback Dirty Inode"),
	PERF_INFO(TRACEPOINT, TP_WRITEBACK_DIRTY_PAGE,	"Writeback Dirty Page"),

	{ 0, 0, 0, NULL }
};

static const perf_tp_info_t perf_tp_info[] = {
	PERF_TP_INFO(TP_SYSCALLS_ENTER,		"raw_syscalls/sys_enter"),
	PERF_TP_INFO(TP_SYSCALLS_EXIT,		"raw_syscalls/sys_exit"),
	PERF_TP_INFO(TP_TLB_FLUSH, 		"tlb/tlb_flush"),
	PERF_TP_INFO(TP_KMALLOC,		"kmem/kmalloc"),
	PERF_TP_INFO(TP_KMALLOC_NODE,		"kmem/kmalloc_node"),
	PERF_TP_INFO(TP_KFREE,			"kmem/kfree"),
	PERF_TP_INFO(TP_KMEM_CACHE_ALLOC,	"kmem/kmem_cache_alloc"),
	PERF_TP_INFO(TP_KMEM_CACHE_ALLOC_NODE,	"kmem/kmem_cache_alloc_node"),
	PERF_TP_INFO(TP_KMEM_CACHE_FREE,	"kmem/kmem_cache_free"),
	PERF_TP_INFO(TP_MM_PAGE_ALLOC,		"kmem/mm_page_alloc"),
	PERF_TP_INFO(TP_MM_PAGE_FREE,		"kmem/mm_page_free"),
	PERF_TP_INFO(TP_RCU_UTILIZATION,	"rcu/rcu_utilization"),
	PERF_TP_INFO(TP_SCHED_MIGRATE_TASK,	"sched/sched_migrate_task"),
	PERF_TP_INFO(TP_SCHED_MOVE_NUMA,	"sched/sched_move_numa"),
	PERF_TP_INFO(TP_SCHED_WAKEUP,		"sched/sched_wakeup"),
	PERF_TP_INFO(TP_SIGNAL_GENERATE,	"signal/signal_generate"),
	PERF_TP_INFO(TP_SIGNAL_DELIVER,		"signal/signal_deliver"),
	PERF_TP_INFO(TP_PAGE_FAULT_USER,	"exceptions/page_fault_user"),
	PERF_TP_INFO(TP_PAGE_FAULT_KERNEL,	"exceptions/page_fault_kernel"),
	PERF_TP_INFO(TP_IRQ_ENTRY,		"irq/irq_handler_entry"),
	PERF_TP_INFO(TP_IRQ_EXIT,		"irq/irq_handler_exit"),
	PERF_TP_INFO(TP_SOFTIRQ_ENTRY,		"irq/softirq_entry"),
	PERF_TP_INFO(TP_SOFTIRQ_EXIT,		"irq/softirq_exit"),
	PERF_TP_INFO(TP_WRITEBACK_DIRTY_INODE,	"writeback/writeback_dirty_inode"),
	PERF_TP_INFO(TP_WRITEBACK_DIRTY_PAGE,	"writeback/writeback_dirty_page"),

	{ 0, NULL }
};

static unsigned long perf_type_tracepoint_resolve_config(const int id)
{
	char path[PATH_MAX];
	size_t i;
	unsigned long config;
	bool not_found = true;
	FILE *fp;

	for (i = 0; perf_tp_info[i].path; i++) {
		if (perf_tp_info[i].id == id) {
			not_found = false;
			break;
		}
	}
	if (not_found)
		return UNRESOLVED;

	(void)snprintf(path, sizeof(path), "/sys/kernel/debug/tracing/events/%s/id",
		perf_tp_info[i].path);
	if ((fp = fopen(path, "r")) == NULL)
		return UNRESOLVED;
	if (fscanf(fp, "%lu", &config) != 1) {
		(void)fclose(fp);
		return UNRESOLVED;
	}
	(void)fclose(fp);

	return config;
}

void perf_init(void)
{
	size_t i;

	for (i = 0; i < STRESS_PERF_MAX; i++) {
		if (perf_info[i].type == PERF_TYPE_TRACEPOINT) {
			perf_info[i].config =
				perf_type_tracepoint_resolve_config(perf_info[i].id);
		}
	}
}

static inline int sys_perf_event_open(
	struct perf_event_attr *attr,
	pid_t pid,
	int cpu,
	int group_fd,
	unsigned long flags)
{
	return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

/*
 *  perf_yaml_label()
 *	turns text into a yaml compatible lable.
 */
static char *perf_yaml_label(char *dst, const char *src, size_t n)
{
	if (n) {
		char *d = dst;
		const char *s = src;

		do {
			if (*s == ' ')
				*d = '_';
			else if (isupper(*s))
				*d = tolower(*s);
			else if (*s)
				*d = *s;
			else {
				while (--n != 0)
					*d++ = 0;
				break;
			}
			s++;
			d++;
		} while (--n != 0);
	}
	return dst;
}

/*
 *  perf_open()
 *	open perf, get leader and perf fd's
 */
int perf_open(stress_perf_t *sp)
{
	size_t i;

	if (!sp)
		return -1;
	if (g_shared->perf.no_perf)
		return -1;

	memset(sp, 0, sizeof(stress_perf_t));
	sp->perf_opened = 0;

	for (i = 0; i < STRESS_PERF_MAX; i++) {
		sp->perf_stat[i].fd = -1;
		sp->perf_stat[i].counter = 0;
	}

	for (i = 0; i < STRESS_PERF_MAX && perf_info[i].label; i++) {
		if (perf_info[i].config != UNRESOLVED) {
			struct perf_event_attr attr;

			memset(&attr, 0, sizeof(attr));
			attr.type = perf_info[i].type;
			attr.config = perf_info[i].config;
			attr.disabled = 1;
			attr.inherit = 1;
			attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED |
					   PERF_FORMAT_TOTAL_TIME_RUNNING;
			attr.size = sizeof(attr);
			sp->perf_stat[i].fd =
				sys_perf_event_open(&attr, 0, -1, -1, 0);
			if (sp->perf_stat[i].fd > -1)
				sp->perf_opened++;
		}
	}
	if (!sp->perf_opened) {
		pthread_spin_lock(&g_shared->perf.lock);
		if (!g_shared->perf.no_perf) {
			pr_dbg("perf_event_open failed, no "
				"perf events [%u]\n", getpid());
			g_shared->perf.no_perf = true;
		}
		pthread_spin_unlock(&g_shared->perf.lock);
		return -1;
	}

	return 0;
}

/*
 *  perf_enable()
 *	enable perf counters
 */
int perf_enable(stress_perf_t *sp)
{
	size_t i;

	if (!sp)
		return -1;
	if (!sp->perf_opened)
		return 0;

	for (i = 0; i < STRESS_PERF_MAX && perf_info[i].label; i++) {
		int fd = sp->perf_stat[i].fd;

		if (fd > -1) {
			if (ioctl(fd, PERF_EVENT_IOC_RESET,
				  PERF_IOC_FLAG_GROUP) < 0) {
				(void)close(fd);
				sp->perf_stat[i].fd = -1;
				continue;
			}
			if (ioctl(fd, PERF_EVENT_IOC_ENABLE,
				  PERF_IOC_FLAG_GROUP) < 0) {
				(void)close(fd);
				sp->perf_stat[i].fd = -1;
			}
		}
	}
	return 0;
}

/*
 *  perf_disable()
 *	disable perf counters
 */
int perf_disable(stress_perf_t *sp)
{
	size_t i;

	if (!sp)
		return -1;
	if (!sp->perf_opened)
		return 0;

	for (i = 0; i < STRESS_PERF_MAX && perf_info[i].label; i++) {
		int fd = sp->perf_stat[i].fd;

		if (fd > -1) {
			if (ioctl(fd, PERF_EVENT_IOC_DISABLE,
			          PERF_IOC_FLAG_GROUP) < 0) {
				(void)close(fd);
				sp->perf_stat[i].fd = -1;
			}
		}
	}
	return 0;
}

/*
 *  perf_close()
 *	read counters and close
 */
int perf_close(stress_perf_t *sp)
{
	size_t i = 0;
	perf_data_t data;
	ssize_t ret;
	int rc = -1;
	double scale;

	if (!sp)
		return -1;
	if (!sp->perf_opened)
		goto out_ok;

	for (i = 0; i < STRESS_PERF_MAX && perf_info[i].label; i++) {
		int fd = sp->perf_stat[i].fd;
		if (fd < 0 ) {
			sp->perf_stat[i].counter = STRESS_PERF_INVALID;
			continue;
		}

		memset(&data, 0, sizeof(data));
		ret = read(fd, &data, sizeof(data));
		if (ret != sizeof(data))
			sp->perf_stat[i].counter = STRESS_PERF_INVALID;
		else {
			/* Ensure we don't get division by zero */
			if (data.time_running == 0) {
				scale = (data.time_enabled == 0) ? 1.0 : 0.0;
			} else {
				scale = (double)data.time_enabled /
					data.time_running;
			}
			sp->perf_stat[i].counter = (uint64_t)
				((double)data.counter * scale);
		}
		(void)close(fd);
		sp->perf_stat[i].fd = -1;
	}

out_ok:
	rc = 0;
	for (; i < STRESS_PERF_MAX; i++)
		sp->perf_stat[i].counter = STRESS_PERF_INVALID;

	return rc;
}

/*
 *  perf_get_counter_by_index()
 *	fetch counter and perf ID via index i
 */
int perf_get_counter_by_index(
	const stress_perf_t *sp,
	const int i,
	uint64_t *counter,
	int *id)
{
	if ((i < 0) || (i >= STRESS_PERF_MAX))
		goto fail;

	if (perf_info[i].label) {
		*id = perf_info[i].id;
		*counter = sp->perf_stat[i].counter;
		return 0;
	}

fail:
	*id = -1;
	*counter = STRESS_PERF_INVALID;
	return -1;
}

/*
 *  perf_get_label_by_index()
 *	fetch label via index i
 */
const char *perf_get_label_by_index(const int i)
{
	if ((i < 0) || (i >= STRESS_PERF_MAX))
		return NULL;

	return perf_info[i].label;
}


/*
 *  perf_get_counter_by_id()
 *	fetch counter and index via perf ID
 */
int perf_get_counter_by_id(
	const stress_perf_t *sp,
	int id,
	uint64_t *counter,
	int *index)
{
	int i;

	for (i = 0; perf_info[i].label; i++) {
		if (perf_info[i].id == id) {
			*index = i;
			*counter = sp->perf_stat[i].counter;
			return 0;
		}
	}

	*index = -1;
	*counter = 0;
	return -1;
}

/*
 *  perf_stat_succeeded()
 *	did perf event open work OK?
 */
bool perf_stat_succeeded(const stress_perf_t *sp)
{
	return sp->perf_opened > 0;
}

typedef struct {
	double		threshold;
	double		scale;
	char 		*suffix;
} perf_scale_t;

static perf_scale_t perf_scale[] = {
	{ THOUSAND,		1.0,		"/sec" },
	{ 100 * THOUSAND,	THOUSAND,	"K/sec" },
	{ 100 * MILLION,	MILLION,	"M/sec" },
	{ 100 * BILLION,	BILLION,	"B/sec" },
	{ 100 * TRILLION,	TRILLION,	"T/sec" },
	{ 100 * QUADRILLION,	QUADRILLION,	"P/sec" },
	{ 100 * QUINTILLION,	QUINTILLION,	"E/sec" },
	{ -1, 			-1,		NULL }
};

/*
 *  perf_stat_scale()
 *	scale a counter by duration seconds
 *	into a human readable form
 */
const char *perf_stat_scale(const uint64_t counter, const double duration)
{
	static char buffer[40];
	char *suffix = "E/sec";
	double scale = QUINTILLION;
	size_t i;

	double scaled =
		duration > 0.0 ? (double)counter / duration : 0.0;

	for (i = 0; perf_scale[i].suffix; i++) {
		if (scaled < perf_scale[i].threshold) {
			suffix = perf_scale[i].suffix;
			scale = perf_scale[i].scale;
			break;
		}
	}
	scaled /= scale;

	(void)snprintf(buffer, sizeof(buffer), "%11.2f %-5s",
		scaled, suffix);

	return buffer;
}

void perf_stat_dump(
	FILE *yaml,
	const stress_t stressors[],
	const proc_info_t procs[STRESS_MAX],
	const int32_t max_procs,
	const double duration)
{
	int32_t i;
	bool no_perf_stats = true;

	setlocale(LC_ALL, "");

	pr_yaml(yaml, "perfstats:\n");

	for (i = 0; i < STRESS_MAX; i++) {
		int p;
		uint64_t counter_totals[STRESS_PERF_MAX];
		uint64_t total_cpu_cycles = 0;
		uint64_t total_cache_refs = 0;
		uint64_t total_branches = 0;
		int ids[STRESS_PERF_MAX];
		bool got_data = false;
		char *munged;

		memset(counter_totals, 0, sizeof(counter_totals));

		/* Sum totals across all instances of the stressor */
		for (p = 0; p < STRESS_PERF_MAX; p++) {
			int32_t j, n = (i * max_procs);
			stress_perf_t *sp = &g_shared->stats[n].sp;

			if (!perf_stat_succeeded(sp))
				continue;

			ids[p] = ~0;
			for (j = 0; j < procs[i].started_procs; j++, n++) {
				uint64_t counter;

				if (perf_get_counter_by_index(sp, p,
				    &counter, &ids[p]) < 0)
					break;
				if (counter == STRESS_PERF_INVALID) {
					counter_totals[p] = STRESS_PERF_INVALID;
					break;
				}
				counter_totals[p] += counter;
				got_data |= (counter > 0);
			}
			if (ids[p] == STRESS_PERF_HW_CPU_CYCLES)
				total_cpu_cycles = counter_totals[p];
			if (ids[p] == STRESS_PERF_HW_CACHE_REFERENCES)
				total_cache_refs = counter_totals[p];
			if (ids[p] == STRESS_PERF_HW_BRANCH_INSTRUCTIONS)
				total_branches = counter_totals[p];
		}

		if (!got_data)
			continue;

		munged = munge_underscore(stressors[i].name);
		pr_inf("%s:\n", munged);
		pr_yaml(yaml, "    - stressor: %s\n", munged);
		pr_yaml(yaml, "      duration: %f\n", duration);

		for (p = 0; p < STRESS_PERF_MAX; p++) {
			const char *l = perf_get_label_by_index(p);
			uint64_t ct = counter_totals[p];

			if (l && (ct != STRESS_PERF_INVALID)) {
				char extra[32];
				char yaml_label[128];
				*extra = '\0';

				no_perf_stats = false;

				if ((ids[p] == STRESS_PERF_HW_INSTRUCTIONS) &&
				    (total_cpu_cycles > 0))
					(void)snprintf(extra, sizeof(extra),
						" (%.3f instr. per cycle)",
						(double)ct / (double)total_cpu_cycles);
				if ((ids[p] == STRESS_PERF_HW_CACHE_MISSES) &&
				     (total_cache_refs > 0))
					(void)snprintf(extra, sizeof(extra),
						" (%5.2f%%)",
						100.0 * (double)ct / (double)total_cache_refs);
				if ((ids[p] == STRESS_PERF_HW_BRANCH_MISSES) &&
				    (total_branches > 0))
					(void)snprintf(extra, sizeof(extra),
						" (%5.2f%%)",
						100.0 * (double)ct / (double)total_branches);

				pr_inf("%'26" PRIu64 " %-23s %s%s\n",
					ct, l, perf_stat_scale(ct, duration),
					extra);

				perf_yaml_label(yaml_label, l, sizeof(yaml_label));
				pr_yaml(yaml, "      %s_total: %" PRIu64
					"\n", yaml_label, ct);
				pr_yaml(yaml, "      %s_per_second: %f\n",
					yaml_label, (double)ct / duration);
			}
		}
		pr_yaml(yaml, "\n");
	}
	if (no_perf_stats) {
		if (geteuid() != 0) {
			char buffer[64];
			int ret;
			bool paranoid = false;
			int level = 0;
			static char *path = "/proc/sys/kernel/perf_event_paranoid";

			ret = system_read(path, buffer, sizeof(buffer) - 1);
			if (ret > 0) {
				if (sscanf(buffer, "%5d", &level) == 1)
					paranoid = true;
			}
			if (paranoid & (level > 1)) {
				pr_inf("Cannot read perf counters, "
					"do not have CAP_SYS_ADMIN capability "
					"or %s is set too high (%d)\n",
					path, level);
			}
		} else {
			pr_inf("perf counters are not available "
				"on this device\n");
		}
	}
}
#endif
