/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King.
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
#include "core-builtin.h"
#include "core-lock.h"
#include "core-perf.h"
#include "core-perf-event.h"

#include <ctype.h>
#include <sys/ioctl.h>

#if defined(HAVE_LINUX_PERF_EVENT_H)
#include <linux/perf_event.h>
#endif

#if defined(HAVE_LOCALE_H)
#include <locale.h>
#endif

#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H) &&	\
    defined(HAVE_SYSCALL)
/* perf enabled systems */

#define THOUSAND	(1.0E3)
#define MILLION		(1.0E6)
#define BILLION		(1.0E9)
#define TRILLION	(1.0E12)
#define QUADRILLION	(1.0E15)
#define QUINTILLION	(1.0E18)
#define SEXTILLION	(1.0E21)
#define SEPTILLION	(1.0E24)

#define UNRESOLVED	(~0UL)

/* used for table of perf events to gather */
typedef struct {
	const unsigned int type;	/* perf types */
	unsigned long int config;	/* perf type specific config */
	const char *path;		/* perf trace point path (only for trace points) */
	const char *label;		/* human readable name for perf type */
} stress_perf_info_t;

/* perf data */
typedef struct {
	uint64_t counter;		/* perf counter */
	uint64_t time_enabled;		/* perf time enabled */
	uint64_t time_running;		/* perf time running */
} stress_perf_data_t;

typedef struct {
	const double	threshold;	/* scaling threshold */
	const double	scale;		/* scaling value */
	const char 	*suffix;	/* scaling suffix, GB, MB, etc */
} stress_perf_scale_t;

/* Tracepoint */
#define PERF_INFO_TP(path, label)	\
	{ PERF_TYPE_TRACEPOINT, UNRESOLVED, path, label }

/* Hardware */
#define PERF_INFO_HW(config, label)	\
	{ PERF_TYPE_HARDWARE, PERF_COUNT_ ## config, NULL, label }

/* Software */
#define PERF_INFO_SW(config, label)	\
	{ PERF_TYPE_SOFTWARE, PERF_COUNT_ ## config, NULL, label }

#define PERF_INFO_HW_CACHE_CONFIG(cache_id, op_id, result_id)	\
	  (PERF_COUNT_HW_CACHE_ ## cache_id) |			\
	  ((PERF_COUNT_HW_CACHE_OP_ ## op_id) << 8) |		\
	  ((PERF_COUNT_HW_CACHE_RESULT_ ## result_id) << 16)	\

/* Hardware Cache */
#define PERF_INFO_HW_C(cache_id, op_id, result_id, label)	\
	{ PERF_TYPE_HW_CACHE, 					\
	  PERF_INFO_HW_CACHE_CONFIG(cache_id, op_id, result_id),\
	  NULL, label }

#define STRESS_PERF_DEFINED(x) STRESS_PERF_COUNT_ ## x

/*
 *  Perf scaling factors
 */
static const stress_perf_scale_t perf_scale[] = {
	{ THOUSAND,		1.0,		"/sec" },
	{ 100 * THOUSAND,	THOUSAND,	"K/sec" },
	{ 100 * MILLION,	MILLION,	"M/sec" },
	{ 100 * BILLION,	BILLION,	"B/sec" },
	{ 100 * TRILLION,	TRILLION,	"T/sec" },
	{ 100 * QUADRILLION,	QUADRILLION,	"P/sec" },
	{ 100 * QUINTILLION,	QUINTILLION,	"E/sec" },
	{ 100 * SEXTILLION,	SEXTILLION,	"Z/sec" },
	{ 100 * SEPTILLION,	SEPTILLION,	"Y/sec" },
	{ -1, 			-1,		NULL }
};

/* perf counters to be read */
static stress_perf_info_t perf_info[STRESS_PERF_MAX] = {
	/*
	 *  Hardware counters
	 */
#if STRESS_PERF_DEFINED(HW_CPU_CYCLES)
	PERF_INFO_HW(HW_CPU_CYCLES,		"CPU Cycles"),
#endif
#if STRESS_PERF_DEFINED(HW_INSTRUCTIONS)
	PERF_INFO_HW(HW_INSTRUCTIONS,		"Instructions"),
#endif
#if STRESS_PERF_DEFINED(HW_BRANCH_INSTRUCTIONS)
	PERF_INFO_HW(HW_BRANCH_INSTRUCTIONS,	"Branch Instructions"),
#endif
#if STRESS_PERF_DEFINED(HW_BRANCH_MISSES)
	PERF_INFO_HW(HW_BRANCH_MISSES,		"Branch Misses"),
#endif
#if STRESS_PERF_DEFINED(HW_STALLED_CYCLES_FRONTEND)
	PERF_INFO_HW(HW_STALLED_CYCLES_FRONTEND,"Stalled Cycles Frontend"),
#endif
#if STRESS_PERF_DEFINED(HW_STALLED_CYCLES_BACKEND)
	PERF_INFO_HW(HW_STALLED_CYCLES_BACKEND,	"Stalled Cycles Backend"),
#endif
#if STRESS_PERF_DEFINED(HW_BUS_CYCLES)
	PERF_INFO_HW(HW_BUS_CYCLES,		"Bus Cycles"),
#endif
#if STRESS_PERF_DEFINED(HW_REF_CPU_CYCLES)
	PERF_INFO_HW(HW_REF_CPU_CYCLES,		"Total Cycles"),
#endif
#if STRESS_PERF_DEFINED(HW_CACHE_REFERENCES)
	PERF_INFO_HW(HW_CACHE_REFERENCES,	"Cache References"),
#endif
#if STRESS_PERF_DEFINED(HW_CACHE_MISSES)
	PERF_INFO_HW(HW_CACHE_MISSES,		"Cache Misses"),
#endif

	/*
	 *  Hardware Cache counters
	 */
#if STRESS_PERF_DEFINED(HW_CACHE_L1D)
	PERF_INFO_HW_C(L1D, READ, ACCESS, 	"Cache L1D Read"),
	PERF_INFO_HW_C(L1D, READ, MISS, 	"Cache L1D Read Miss"),
	PERF_INFO_HW_C(L1D, WRITE, ACCESS, 	"Cache L1D Write"),
	PERF_INFO_HW_C(L1D, WRITE, MISS, 	"Cache L1D Write Miss"),
	PERF_INFO_HW_C(L1D, PREFETCH, ACCESS, 	"Cache L1D Prefetch"),
	PERF_INFO_HW_C(L1D, PREFETCH, MISS, 	"Cache L1D Prefetch Miss"),
#endif

#if STRESS_PERF_DEFINED(HW_CACHE_L1I)
	PERF_INFO_HW_C(L1I, READ, ACCESS, 	"Cache L1I Read"),
	PERF_INFO_HW_C(L1I, READ, MISS, 	"Cache L1I Read Miss"),
	PERF_INFO_HW_C(L1I, WRITE, ACCESS, 	"Cache L1I Write"),
	PERF_INFO_HW_C(L1I, WRITE, MISS, 	"Cache L1I Write Miss"),
	PERF_INFO_HW_C(L1I, PREFETCH, ACCESS,	"Cache L1I Prefetch"),
	PERF_INFO_HW_C(L1I, PREFETCH, MISS,	"Cache L1I Prefetch Miss"),
#endif

#if STRESS_PERF_DEFINED(HW_CACHE_LL)
	PERF_INFO_HW_C(LL, READ, ACCESS,	"Cache LL Read"),
	PERF_INFO_HW_C(LL, READ, MISS,		"Cache LL Read Miss"),
	PERF_INFO_HW_C(LL, WRITE, ACCESS,	"Cache LL Write"),
	PERF_INFO_HW_C(LL, WRITE, MISS,		"Cache LL Write Miss"),
	PERF_INFO_HW_C(LL, PREFETCH, ACCESS,	"Cache LL Prefetch"),
	PERF_INFO_HW_C(LL, PREFETCH, MISS,	"Cache LL Prefetch Miss"),
#endif

#if STRESS_PERF_DEFINED(HW_CACHE_DTLB)
	PERF_INFO_HW_C(DTLB, READ, ACCESS, 	"Cache DTLB Read"),
	PERF_INFO_HW_C(DTLB, READ, MISS, 	"Cache DTLB Read Miss"),
	PERF_INFO_HW_C(DTLB, WRITE, ACCESS, 	"Cache DTLB Write"),
	PERF_INFO_HW_C(DTLB, WRITE, MISS, 	"Cache DTLB Write Miss"),
	PERF_INFO_HW_C(DTLB, PREFETCH, ACCESS, 	"Cache DTLB Prefetch"),
	PERF_INFO_HW_C(DTLB, PREFETCH, MISS,	"Cache DTLB Prefetch Miss"),
#endif

#if STRESS_PERF_DEFINED(HW_CACHE_ITLB)
	PERF_INFO_HW_C(ITLB, READ, ACCESS,	"Cache ITLB Read"),
	PERF_INFO_HW_C(ITLB, READ, MISS,	"Cache ITLB Read Miss"),
	PERF_INFO_HW_C(ITLB, WRITE, ACCESS,	"Cache ITLB Write"),
	PERF_INFO_HW_C(ITLB, WRITE, MISS,	"Cache ITLB Write Miss"),
	PERF_INFO_HW_C(ITLB, PREFETCH, ACCESS,	"Cache ITLB Prefetch"),
	PERF_INFO_HW_C(ITLB, PREFETCH, MISS,	"Cache IILB Prefetch Miss"),
#endif

#if STRESS_PERF_DEFINED(HW_CACHE_BPU)
	PERF_INFO_HW_C(BPU, READ, ACCESS,	"Cache BPU Read"),
	PERF_INFO_HW_C(BPU, READ, MISS,		"Cache BPU Read Miss"),
	PERF_INFO_HW_C(BPU, WRITE, ACCESS,	"Cache BPU Write"),
	PERF_INFO_HW_C(BPU, WRITE, MISS,	"Cache BPU Write Miss"),
	PERF_INFO_HW_C(BPU, PREFETCH, ACCESS,	"Cache BPU Prefetch"),
	PERF_INFO_HW_C(BPU, PREFETCH, MISS,	"Cache DILB Prefetch Miss"),
#endif

#if STRESS_PERF_DEFINED(HW_CACHE_NODE)
	PERF_INFO_HW_C(NODE, READ, ACCESS,	"Cache NODE Read"),
	PERF_INFO_HW_C(NODE, READ, MISS,	"Cache NODE Read Miss"),
	PERF_INFO_HW_C(NODE, WRITE, ACCESS,	"Cache NODE Write"),
	PERF_INFO_HW_C(NODE, WRITE, MISS,	"Cache NODE Write Miss"),
	PERF_INFO_HW_C(NODE, PREFETCH, ACCESS,	"Cache NODE Prefetch"),
	PERF_INFO_HW_C(NODE, PREFETCH, MISS,	"Cache DILB Prefetch Miss"),
#endif

	/*
	 *  Software counters
	 */
#if STRESS_PERF_DEFINED(SW_CPU_CLOCK)
	PERF_INFO_SW(SW_CPU_CLOCK,		"CPU Clock"),
#endif
#if STRESS_PERF_DEFINED(SW_TASK_CLOCK)
	PERF_INFO_SW(SW_TASK_CLOCK,		"Task Clock"),
#endif
#if STRESS_PERF_DEFINED(SW_PAGE_FAULTS)
	PERF_INFO_SW(SW_PAGE_FAULTS,		"Page Faults Total"),
#endif
#if STRESS_PERF_DEFINED(SW_PAGE_FAULTS_MIN)
	PERF_INFO_SW(SW_PAGE_FAULTS_MIN,	"Page Faults Minor"),
#endif
#if STRESS_PERF_DEFINED(SW_PAGE_FAULTS_MAJ)
	PERF_INFO_SW(SW_PAGE_FAULTS_MAJ,	"Page Faults Major"),
#endif
#if STRESS_PERF_DEFINED(SW_CONTEXT_SWITCHES)
	PERF_INFO_SW(SW_CONTEXT_SWITCHES,	"Context Switches"),
#endif
#if STRESS_PERF_DEFINED(SW_CGROUP_SWITCHES)
	PERF_INFO_SW(SW_CGROUP_SWITCHES,	"Cgroup Switches"),
#endif
#if STRESS_PERF_DEFINED(SW_CPU_MIGRATIONS)
	PERF_INFO_SW(SW_CPU_MIGRATIONS,		"CPU Migrations"),
#endif
#if STRESS_PERF_DEFINED(SW_ALIGNMENT_FAULTS)
	PERF_INFO_SW(SW_ALIGNMENT_FAULTS,	"Alignment Faults"),
#endif
#if STRESS_PERF_DEFINED(SW_EMULATION_FAULTS)
	PERF_INFO_SW(SW_EMULATION_FAULTS,	"Emulation Faults"),
#endif
	/*
	 *  Tracepoint counters
 	 */
	PERF_INFO_TP("exceptions/page_fault_user",	"Page Faults User"),
	PERF_INFO_TP("exceptions/page_fault_kernel",	"Page Faults Kernel"),
	PERF_INFO_TP("raw_syscalls/sys_enter",		"System Call Enter"),
	PERF_INFO_TP("raw_syscalls/sys_exit",		"System Call Exit"),

	/* This perf metric causes 5.4+ kernel hangs, disable it for now */
#if 1
	PERF_INFO_TP("tlb/tlb_flush",			"TLB Flushes"),
#endif
	PERF_INFO_TP("swiotlb/swiotlb_bounced",		"Software I/O TLB Bounces"),

	PERF_INFO_TP("kmem/kmalloc",			"Kmalloc"),
	PERF_INFO_TP("kmem/kmalloc_node",		"Kmalloc Node"),
	PERF_INFO_TP("kmem/kfree",			"Kfree"),
	PERF_INFO_TP("kmem/kmem_cache_alloc",		"Kmem Cache Alloc"),
	PERF_INFO_TP("kmem/kmem_cache_alloc_node",	"Kmem Cache Alloc Node"),
	PERF_INFO_TP("kmem/kmem_cache_free",		"Kmem Cache Free"),
	PERF_INFO_TP("kmem/mm_page_alloc",		"MM Page Alloc"),
	PERF_INFO_TP("kmem/mm_page_free",		"MM Page Free"),

	PERF_INFO_TP("mmap_lock/mmap_lock_start_locking","MMAP lock start"),
	PERF_INFO_TP("mmap_lock/mmap_lock_released",	"MMAP lock release"),
	PERF_INFO_TP("mmap_lock/mmap_lock_acquire_returned","MMAP lock acquire"),

	PERF_INFO_TP("rcu/rcu_utilization",		"RCU Utilization"),
	PERF_INFO_TP("rcu/rcu_stall_warning",		"RCU Stall Warning"),
	PERF_INFO_TP("rcu/rcu_preempt_task",		"RCU Preempt Task"),

	PERF_INFO_TP("sched/sched_migrate_task",	"Sched Migrate Task"),
	PERF_INFO_TP("sched/sched_move_numa",		"Sched Move NUMA"),
	PERF_INFO_TP("sched/sched_wakeup",		"Sched Wakeup"),
	PERF_INFO_TP("sched/sched_process_exec",	"Sched Proc Exec"),
	PERF_INFO_TP("sched/sched_process_exit",	"Sched Proc Exit"),
	PERF_INFO_TP("sched/sched_process_fork",	"Sched Proc Fork"),
	PERF_INFO_TP("sched/sched_process_free",	"Sched Proc Free"),
	PERF_INFO_TP("sched/sched_process_hang",	"Sched Proc Hang"),
	PERF_INFO_TP("sched/sched_process_wait",	"Sched Proc Wait"),
	PERF_INFO_TP("sched/sched_switch",		"Sched Switch"),
	PERF_INFO_TP("sched/sched_wait_task",		"Sched Wait Task"),

	PERF_INFO_TP("task/task_newtask",		"New Task"),
	PERF_INFO_TP("context_tracking/user_enter",	"Context User Enter"),
	PERF_INFO_TP("context_tracking/user_exit",	"Context User Exit"),

	PERF_INFO_TP("signal/signal_generate",		"Signal Generate"),
	PERF_INFO_TP("signal/signal_deliver",		"Signal Deliver"),

	PERF_INFO_TP("irq/irq_handler_entry",		"IRQ Entry"),
	PERF_INFO_TP("irq/irq_handler_exit",		"IRQ Exit"),
	PERF_INFO_TP("irq/softirq_entry",		"Soft IRQ Entry"),
	PERF_INFO_TP("irq/softirq_exit",		"Soft IRQ Exit"),
	PERF_INFO_TP("irq/tasklet_entry",		"Tasklet Entry"),
	PERF_INFO_TP("irq/tasklet_exit",		"Tasklet Exit"),
	PERF_INFO_TP("nmi/nmi_handler",			"NMI handler"),

	PERF_INFO_TP("ipi/ipi_entry",			"IPI Entry"),
	PERF_INFO_TP("ipi/ipi_raise",			"IPI Raise"),
	PERF_INFO_TP("ipi/ipi_send_cpu",		"IPI Send CPU"),
	PERF_INFO_TP("ipi/ipi_send_cpumask",		"IPI Send CPU Mask"),
	PERF_INFO_TP("ipi/ipi_exit",			"IPI Exit"),

	PERF_INFO_TP("irq_vectors/x86_platform_ipi_entry", "x86 Platform IPI Entry"),
	PERF_INFO_TP("irq_vectors/call_function_entry", "Call Function Entry"),
	PERF_INFO_TP("irq_vectors/irq_work_entry",	"IRQ Work Entry"),
	PERF_INFO_TP("irq_vectors/local_timer_entry",	"Local Timer Entry"),
	PERF_INFO_TP("irq_vectors/reschedule_entry",	"Reschedule Entry"),
	PERF_INFO_TP("irq_vectors/thermal_apic_entry",	"Thermal APIC Entry"),

	PERF_INFO_TP("block/block_bio_complete",	"Block BIO Complete"),
#if 1
	PERF_INFO_TP("iomap/iomap_readpage",		"IOMAP Read Page"),
	PERF_INFO_TP("iomap/iomap_writepage",		"IOMAP Write Page"),
#endif

	PERF_INFO_TP("io_uring/io_uring_submit_sqe",	"IO uring submit SQE"),
	PERF_INFO_TP("io_uring/io_uring_submit_req",	"IO uring submit REQ"),
	PERF_INFO_TP("io_uring/io_uring_complete",	"IO uring complete"),

	PERF_INFO_TP("writeback/writeback_dirty_inode",	"Writeback Dirty Inode"),
	PERF_INFO_TP("writeback/writeback_dirty_page",	"Writeback Dirty Page"),
	PERF_INFO_TP("writeback/writeback_dirty_folio",	"Writeback Dirty Folio"),

	PERF_INFO_TP("migrate/mm_migrate_pages",	"Migrate MM Pages"),

	PERF_INFO_TP("skb/consume_skb",			"SKB Consume"),
	PERF_INFO_TP("skb/kfree_skb",			"SKB Kfree"),

	PERF_INFO_TP("lock/contention_begin",		"Lock Contention Begin"),
	PERF_INFO_TP("lock/contention_end",		"Lock Contention End"),

	PERF_INFO_TP("maple_tree/ma_op",		"Maple Tree Op"),
	PERF_INFO_TP("maple_tree/ma_read",		"Maple Tree Read"),
	PERF_INFO_TP("maple_tree/ma_write",		"Maple Tree Write"),

	PERF_INFO_TP("qdisc/qdisc_enqueue",		"Qdisc Enqueue"),
	PERF_INFO_TP("qdisc/qdisc_dequeue",		"Qdisc Dequeue"),

	PERF_INFO_TP("msr/read_msr",			"MSR read"),
	PERF_INFO_TP("msr/write_msr",			"MSR write"),
	PERF_INFO_TP("msr/rdpmc",			"PMC read"),

	PERF_INFO_TP("iommu/io_page_fault",		"IOMMU IO Page Fault"),
	PERF_INFO_TP("iommu/map",			"IOMMU Map"),
	PERF_INFO_TP("iommu/unmap",			"IOMMU Unmap"),

	PERF_INFO_TP("filemap/mm_filemap_add_to_page_cache",		"Filemap Page-Cache Add"),
	PERF_INFO_TP("filemap/mm_filemap_delete_from_page_cache",	"Filemap Page-Cache Del"),
	PERF_INFO_TP("filemap/mm_filemap_fault",	"Filemap Page Fault"),
	PERF_INFO_TP("filemap/mm_filemap_map_pages",	"Filemap Map Pages"),

	PERF_INFO_TP("oom/compact_retry",		"OOM Compact Retry"),
	PERF_INFO_TP("oom/wake_reaper",			"OOM Wake Reaper"),
	PERF_INFO_TP("oom/mark_victim",			"OOM Mark Victim"),
	PERF_INFO_TP("oom/oom_score_adj_update",	"OOM Score Adjust Update"),

	PERF_INFO_TP("thermal/thermal_zone_trip",	"Thermal Zone Trip"),

	{ 0, 0, NULL, NULL }
};

static inline size_t stress_perf_info_find(const unsigned int type, const unsigned long int config)
{
	size_t i;

	for (i = 0; (i < STRESS_PERF_MAX) && perf_info[i].label; i++) {
		if ((perf_info[i].type == type) && (perf_info[i].config == config))
			return i;
	}
	return STRESS_PERF_MAX;
}

/*
 *  stress_perf_type_tracepoint_resolve_config()
 *	resolve tracing event config value
 */
static inline void stress_perf_type_tracepoint_resolve_config(stress_perf_info_t *pi)
{
	char path[PATH_MAX];
	unsigned long int config;
	FILE *fp;

	if (!pi->path)
		return;

	(void)snprintf(path, sizeof(path), "/sys/kernel/debug/tracing/events/%s/id",
		pi->path);
	if ((fp = fopen(path, "r")) == NULL)
		return;
	if (fscanf(fp, "%lu", &config) != 1) {
		(void)fclose(fp);
		return;
	}
	(void)fclose(fp);

	pi->config = config;
}

/*
 *  stress_perf_init()
 *	perf initialize, resolve all configs
 */
void stress_perf_init(void)
{
	size_t i;

	for (i = 0; i < STRESS_PERF_MAX; i++) {
		if (perf_info[i].type == PERF_TYPE_TRACEPOINT) {
			stress_perf_type_tracepoint_resolve_config(&perf_info[i]);
		}
	}
}

/*
 *  stress_sys_perf_event_open()
 *	perf_event_open syscall wrapper
 */
static inline int stress_sys_perf_event_open(
	struct perf_event_attr *attr,
	pid_t pid,
	int cpu,
	int group_fd,
	unsigned long int flags)
{
	return (int)syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

/*
 *  stress_perf_yaml_label()
 *	turns text into a yaml compatible label.
 */
static char *stress_perf_yaml_label(char *dst, const char *src, const size_t n)
{
	if (n) {
		char *d = dst;
		const char *s = src;
		size_t i = n;

		do {
			unsigned char ch = (unsigned char)*s;

			if (ch == ' ')
				*d = '_';
			else if (isupper(ch))
				*d = (char)tolower(ch);
			else if (ch)
				*d = (char)ch;
			else {
				while (--i != 0)
					*d++ = 0;
				break;
			}
			s++;
			d++;
		} while (--i != 0);
	}
	return dst;
}

/*
 *  stress_perf_open()
 *	open perf, get leader and perf fd's
 */
int stress_perf_open(stress_perf_t *sp)
{
	size_t i;

	if (!sp)
		return -1;
	if (g_shared->perf.no_perf)
		return -1;

	(void)shim_memset(sp, 0, sizeof(*sp));
	sp->perf_opened = 0;

	for (i = 0; i < STRESS_PERF_MAX; i++) {
		sp->perf_stat[i].fd = -1;
		sp->perf_stat[i].counter = 0;
	}

	for (i = 0; (i < STRESS_PERF_MAX) && perf_info[i].label; i++) {
		if (perf_info[i].config != UNRESOLVED) {
			struct perf_event_attr attr;

			(void)shim_memset(&attr, 0, sizeof(attr));
			attr.type = perf_info[i].type;
			attr.config = perf_info[i].config;
			attr.disabled = 1;
			attr.inherit = 1;
			attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED |
					   PERF_FORMAT_TOTAL_TIME_RUNNING;
			attr.size = sizeof(attr);
			sp->perf_stat[i].fd =
				stress_sys_perf_event_open(&attr, 0, -1, -1, 0);
			if (sp->perf_stat[i].fd > -1)
				sp->perf_opened++;
		}
	}
	if (!sp->perf_opened) {
		int ret;

		ret = stress_lock_acquire(g_shared->perf.lock);
		if (ret) {
			pr_dbg("perf: lock on perf lock failed\n");
			return -1;
		}
		if (!g_shared->perf.no_perf) {
			pr_dbg("perf: perf_event_open failed, no "
				"perf events [%u]\n", getpid());
			g_shared->perf.no_perf = true;
		}
		ret = stress_lock_release(g_shared->perf.lock);
		if (ret) {
			pr_dbg("perf: unlock on perf lock failed\n");
			return -1;
		}
		return -1;
	}

	return 0;
}

/*
 *  stress_perf_enable()
 *	enable perf counters
 */
int stress_perf_enable(stress_perf_t *sp)
{
	size_t i;

	if (!sp)
		return -1;
	if (!sp->perf_opened)
		return 0;

	for (i = 0; (i < STRESS_PERF_MAX) && perf_info[i].label; i++) {
		const int fd = sp->perf_stat[i].fd;

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
 *  stress_perf_disable()
 *	disable perf counters
 */
int stress_perf_disable(stress_perf_t *sp)
{
	size_t i;

	if (!sp)
		return -1;
	if (!sp->perf_opened)
		return 0;

	for (i = 0; (i < STRESS_PERF_MAX) && perf_info[i].label; i++) {
		const int fd = sp->perf_stat[i].fd;

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
 *  stress_perf_close()
 *	read counters and close
 */
int stress_perf_close(stress_perf_t *sp)
{
	size_t i = 0;
	stress_perf_data_t data;
	ssize_t ret;
	double scale;

	if (!sp)
		return -1;
	if (!sp->perf_opened)
		goto out_ok;

	for (i = 0; (i < STRESS_PERF_MAX) && perf_info[i].label; i++) {
		const int fd = sp->perf_stat[i].fd;

		if (fd < 0 ) {
			sp->perf_stat[i].counter = STRESS_PERF_INVALID;
			continue;
		}

		(void)shim_memset(&data, 0, sizeof(data));
		ret = read(fd, &data, sizeof(data));
		if (ret != sizeof(data))
			sp->perf_stat[i].counter = STRESS_PERF_INVALID;
		else {
			/* Ensure we don't get division by zero */
			if (data.time_running == 0) {
				scale = (data.time_enabled == 0) ? 1.0 : 0.0;
			} else {
				scale = (double)data.time_enabled /
					(double)data.time_running;
			}
			sp->perf_stat[i].counter = (uint64_t)
				((double)data.counter * scale);
		}
		(void)close(fd);
		sp->perf_stat[i].fd = -1;
	}

out_ok:
	for (; i < STRESS_PERF_MAX; i++)
		sp->perf_stat[i].counter = STRESS_PERF_INVALID;

	return 0;
}

/*
 *  stress_perf_stat_succeeded()
 *	did perf event open work OK?
 */
static bool stress_perf_stat_succeeded(const stress_perf_t *sp)
{
	return sp->perf_opened > 0;
}

/*
 *  stress_perf_stat_scale()
 *	scale a counter by duration seconds
 *	into a human readable form
 */
static const char *stress_perf_stat_scale(const uint64_t counter, const double duration)
{
	static char buffer[40];
	const char *suffix = "E/sec";
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

	(void)snprintf(buffer, sizeof(buffer), "%11.3f %-5s",
		scaled, suffix);

	return buffer;
}

/*
 *  Compare type + config relative to another reference type and config
 */
typedef struct {
	const unsigned int	type;
	const unsigned long int	config;
	const unsigned int	ref_type;
	const unsigned long int	ref_config;
	const bool		percent;	/* scale by 100.0 for percentages? */
	const char 		*fmt;		/* snprintf format */
} perf_relative_t;

static const perf_relative_t perf_relatives[] = {
	{ PERF_TYPE_HARDWARE,	PERF_COUNT_HW_INSTRUCTIONS,
	  PERF_TYPE_HARDWARE,	PERF_COUNT_HW_CPU_CYCLES,
	  false, " (%.3f instr. per cycle)" },
	{ PERF_TYPE_HARDWARE,	PERF_COUNT_HW_CACHE_MISSES,
	  PERF_TYPE_HARDWARE,	PERF_COUNT_HW_CACHE_REFERENCES,
	  true, " (%6.3f%%)" },
	{ PERF_TYPE_HARDWARE,	PERF_COUNT_HW_BRANCH_MISSES,
	  PERF_TYPE_HARDWARE,	PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
	  true, " (%6.3f%%)" },
	{ PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(L1D, READ, MISS),
	  PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(L1D, READ, ACCESS),
	  true, " (%6.3f%%)" },
	{ PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(LL, READ, MISS),
	  PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(LL, READ, ACCESS),
	  true, " (%6.3f%%)" },
	{ PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(LL, WRITE, MISS),
	  PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(LL, WRITE, ACCESS),
	  true, " (%6.3f%%)" },
	{ PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(DTLB, READ, MISS),
	  PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(DTLB, READ, ACCESS),
	  true, " (%6.3f%%)" },
	{ PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(DTLB, WRITE, MISS),
	  PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(DTLB, WRITE, ACCESS),
	  true, " (%6.3f%%)" },
	{ PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(ITLB, READ, MISS),
	  PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(ITLB, READ, ACCESS),
	  true, " (%6.3f%%)" },
	{ PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(BPU, READ, MISS),
	  PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(BPU, READ, ACCESS),
	  true, " (%6.3f%%)" },
	{ PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(NODE, READ, MISS),
	  PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(NODE, READ, ACCESS),
	  true, " (%6.3f%%)" },
	{ PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(NODE, WRITE, MISS),
	  PERF_TYPE_HW_CACHE,	PERF_INFO_HW_CACHE_CONFIG(NODE, WRITE, ACCESS),
	  true, " (%6.3f%%)" },
};

/*
 *  stress_perf_stat_dump()
 *	emit perf statistics
 */
void stress_perf_stat_dump(FILE *yaml, stress_stressor_t *stressors_list, const double duration)
{
	bool no_perf_stats = true;
	stress_stressor_t *ss;

#if defined(HAVE_LOCALE_H)
	(void)setlocale(LC_ALL, "");
#endif

	pr_yaml(yaml, "perfstats:\n");

	for (ss = stressors_list; ss; ss = ss->next) {
		int p;
		uint64_t counter_totals[STRESS_PERF_MAX];
		bool got_data = false;
		stress_perf_t *sp;

		if (ss->ignore.run)
			continue;
		if (!ss->stats)
			continue;
		sp = &ss->stats[0]->sp;
		if (!stress_perf_stat_succeeded(sp))
			continue;

		(void)shim_memset(counter_totals, 0, sizeof(counter_totals));

		/* Sum totals across all instances of the stressor */
		for (p = 0; (p < STRESS_PERF_MAX) && perf_info[p].label; p++) {
			int32_t j;

			for (j = 0; j < ss->instances; j++) {
				const uint64_t counter = sp->perf_stat[p].counter;

				if (counter == STRESS_PERF_INVALID) {
					counter_totals[p] = STRESS_PERF_INVALID;
					break;
				}
				counter_totals[p] += counter;
				got_data |= (counter > 0);
			}
		}

		if (!got_data)
			continue;

		pr_inf("%s:\n", ss->stressor->name);
		pr_yaml(yaml, "    - stressor: %s\n", ss->stressor->name);
		pr_yaml(yaml, "      duration: %f\n", duration);

		for (p = 0; (p < STRESS_PERF_MAX) && perf_info[p].label; p++) {
			const char *label = perf_info[p].label;
			const uint64_t ct = counter_totals[p];

			if (label && (ct != STRESS_PERF_INVALID)) {
				char extra[32];
				char yaml_label[128];
				*extra = '\0';
				size_t i;

				no_perf_stats = false;

				for (i = 0; i < SIZEOF_ARRAY(perf_relatives); i++) {
					if ((perf_info[p].type == perf_relatives[i].type) &&
					    (perf_info[p].config == perf_relatives[i].config)) {
						const size_t idx = stress_perf_info_find(
									perf_relatives[i].ref_type,
									perf_relatives[i].ref_config);

						if ((idx < STRESS_PERF_MAX) &&
						    (counter_totals[idx] > 0))
							(void)snprintf(extra, sizeof(extra),
									perf_relatives[i].fmt,
									(perf_relatives[i].percent ? 100.0 : 1.0) *
									(double)ct / (double)counter_totals[idx]);
					}
				}

				pr_inf("%'26" PRIu64 " %-24s %s%s\n",
					ct, label, stress_perf_stat_scale(ct, duration),
					extra);

				*yaml_label = '\0';
				stress_perf_yaml_label(yaml_label, label, sizeof(yaml_label));
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
			ssize_t ret;
			bool paranoid = false;
			int level = 0;
			static const char *path = "/proc/sys/kernel/perf_event_paranoid";

			ret = stress_system_read(path, buffer, sizeof(buffer) - 1);
			if (ret > 0) {
				if (sscanf(buffer, "%5d", &level) == 1)
					paranoid = true;
			}
			if (paranoid && (level > 1)) {
				pr_inf("cannot read perf counters, "
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
