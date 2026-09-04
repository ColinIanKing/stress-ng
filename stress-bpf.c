/*.
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-capabilities.h"
#include "core-mmap.h"

#if defined(HAVE_LINUX_BPF_H)
#include <linux/bpf.h>
#endif

#define MIN_BPF_PROG_SIZE	(2)
#define MAX_BPF_PROG_SIZE	(1024* 1024)
#define DEFAULT_BPF_PROG_SIZE	(2048)

/*
 *  number of unique bpf instruction to cache, must not
 *  be larger than 2^(stress_bpf_insn_node.next_idx * 8)
 */
#define MAX_UNIQUE_INSNS	(8192)

/* prime size hash table of unique bpf instructions */
#define BPF_HASH_TABLE_SIZE	(65537)

/* number of bpf opcodes */
#define MAX_BPF_OPCODES		(256)

/*
 *  we only cache the BPF opcode and
 *  dst and src registers, so use
 *  stress_bpf_cached_insn_t for storage
 */
typedef struct stress_bpf_cached_insn {
	uint8_t	code;
	uint8_t dst_reg:4;
	uint8_t src_reg:4;
} stress_bpf_cached_insn_t;

/*
 *  hash table entry of a valid bpf instruction
 *    next_idx indexes into unique_insns[] as this
 *    is smaller than a pointer making this struct
 *    small and more cache efficient.
 */
typedef struct stress_bpf_insn_node {
	uint16_t next_idx;
	stress_bpf_cached_insn_t insn;
} stress_bpf_insn_node_t;

static stress_bpf_insn_node_t **stress_bpf_insn_hash_table;
static stress_bpf_insn_node_t *unique_insns;
static size_t unique_insns_count;

/*
 *  stress_bpf_hash_insn()
 *	bpf cached instruction -> hash index
 */
static inline size_t ALWAYS_INLINE stress_bpf_hash_insn(stress_bpf_cached_insn_t *insn)
{
	register size_t hash = insn->code |
	       (((size_t)insn->dst_reg) << 8) |
	       (((size_t)insn->src_reg) << 12);
	return hash % (size_t)BPF_HASH_TABLE_SIZE;
}

/*
 *  stress_bpf_insn_add()
 *	add a bpf instruction to the cache
 */
static void OPTIMIZE3 stress_bpf_insn_add(stress_bpf_cached_insn_t *insn)
{
	register stress_bpf_insn_node_t *insn_node;
	register uint32_t idx;
	register uint32_t next_idx;

	/* any free unique instructions in table? */
	if (UNLIKELY(unique_insns_count >= MAX_UNIQUE_INSNS))
		return;

	idx = stress_bpf_hash_insn(insn);
	insn_node = stress_bpf_insn_hash_table[idx];

	while (insn_node) {
		next_idx = insn_node->next_idx;
		if (shim_memcmp(insn, &insn_node->insn, sizeof(*insn)) == 0)
			break;
		insn_node = stress_bpf_insn_hash_table[next_idx];
	}
	if (insn_node)
		return;	/* don't add duplicates */

	insn_node = &unique_insns[unique_insns_count++];
	insn_node->next_idx = idx;
	insn_node->insn = *insn;
	stress_bpf_insn_hash_table[idx] = insn_node;
	return;
}

static const stress_help_t help[] = {
	{ NULL, "bpf N",	"start N workers exercising random BPF instructions" },
	{ NULL, "bpf-max",	"specify maximum number of BPF instructions " },
	{ NULL,	"bpf-ops N",	"stop after N BPF system calls" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_bpf_max, "bpf-max", TYPE_ID_SIZE_T, MIN_BPF_PROG_SIZE, MAX_BPF_PROG_SIZE, NULL },
	END_OPT,
};

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("lock-contention"),
	STRESS_EX_FEATURE("syscall-rate"),
	STRESS_EX_FEATURE("system-time"),

	STRESS_EX_SYSCALL("bpf"),

	STRESS_EX_END,
};

/*
 *  stress_bpf_supported()
 *      check if we can run this with SHIM_CAP_SYS_ADMIN capability
 */
static int stress_bpf_supported(const char *name)
{
	if (!stress_capabilities_check(SHIM_CAP_BPF)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_BPF "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

#if defined(HAVE_LINUX_BPF_H) &&	\
    defined(__NR_bpf) &&		\
    defined(__linux__)

static int stress_bpf_size_max;
static double stress_bpf_duration;
static double stress_bpf_insns;

/*
 *  stress_sys_bpf()
 *  	bpf system call
 */
static inline int ALWAYS_INLINE stress_sys_bpf(
	enum bpf_cmd cmd,
	union bpf_attr *attr,
	unsigned int size)
{
	return (int)syscall(__NR_bpf, cmd, attr, size);
}

/*
 *  stress_bpf_prog_load()
 *	load a bpf program
 */
static inline int ALWAYS_INLINE stress_bpf_prog_load(
	enum bpf_prog_type type,
	const struct bpf_insn *insns,
	const int insn_cnt,
	const int version)
{
	union bpf_attr attr = {
		.prog_type = type,
		.insns = (uint64_t)insns,
		.insn_cnt = insn_cnt,
		.license = (uint64_t)"GPL",
		.log_buf = 0,
		.log_size = 0,
		.log_level = 0,
		.kern_version = version,
	};

	return stress_sys_bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
}

/*
 *  stress_bpf_push_op()
 *  	push a new bpf instruction onto end of
 *  	bpf code and try and load it/JIT it.
 */
static int OPTIMIZE3 stress_bpf_push_op(
	stress_args_t *args,
	struct bpf_insn *insns,
	const size_t insns_max,
	const int version,
	const size_t len,
	const int next_opcode)
{
	register uint8_t opcode = next_opcode;
	register int i;
	struct bpf_insn saved;
	const int prog_len = len + 2;

	if (UNLIKELY(len >= insns_max - 1))
		return 0;

	saved = insns[len];

	for (i = 0; i < MAX_BPF_OPCODES; i++) {
		int fd;
		bool get_cached;
		double t;

		if (UNLIKELY(!stress_continue(args)))
			return 0;

		get_cached = ((unique_insns_count > 0) && stress_mwc8() > 128);
		if (get_cached) {
			/* fetch an existing cached instruction */
			register const size_t idx = stress_mwcsizemodn(unique_insns_count);
			register stress_bpf_cached_insn_t *insn= &unique_insns[idx].insn;

			insns[len].code = insn->code;
			insns[len].dst_reg = insn->dst_reg;
			insns[len].src_reg = insn->src_reg;
			insns[len].off = 0;
			insns[len].imm = 0;
		} else {
			/* try and use a new one */
			insns[len].code = opcode;
			insns[len].dst_reg = 0;
			insns[len].src_reg = stress_mwc8modn(11);
			insns[len].off = 0;
			insns[len].imm = 0;
		}

		insns[len + 1].code = BPF_JMP | BPF_EXIT;
		insns[len + 1].dst_reg = 0;
		insns[len + 1].src_reg = 0;
		insns[len + 1].off = 0;
		insns[len + 1].imm = 0;

		t = stress_time_now();
		fd = stress_bpf_prog_load(BPF_PROG_TYPE_KPROBE, insns, prog_len, version);
		stress_bogo_inc(args);
		if (fd == -1) {
			if (UNLIKELY(errno == ENOSYS)) {
				pr_inf_skip("%s: bpf() system call not supported, errno=%d (%s), "
					"skipping stressor\n", args->name, errno, strerror(errno));
				return -1;
			}
		} else {
			stress_bpf_duration += stress_time_now() - t;
			stress_bpf_insns += (double)prog_len;

			if (stress_bpf_size_max < prog_len)
				stress_bpf_size_max = prog_len;
			(void)close(fd);

			/* add new instruction if it wasn't already cached */
			if (!get_cached) {
				stress_bpf_cached_insn_t insn;

				insn.code = insns[len].code;
				insn.dst_reg = insns[len].dst_reg;
				insn.src_reg = insns[len].src_reg;
				stress_bpf_insn_add(&insn);
			}

			/* ..and try another instruction */
			stress_bpf_push_op(args, insns, insns_max, version, len + 1, opcode + 1);
		}
		opcode++;
	}
	insns[len] = saved;
	return 0;
}

/*
 *  stress_bpf_kernel_version_binary()
 *	get kernel version in hex value 0xxyyzz
 *	where xx = major, yy=minor, zz=patch level
 */
static int stress_bpf_kernel_version_binary(void)
{
	int version = stress_kernel_release_get();

	if (LIKELY(version > 0)) {
		const int major = (version / 10000) % 100;
		const int minor = (version / 100) % 100;
		const int patch = (version) % 100;

		version = (major << 16) | (minor << 8) | patch;
	}
	return version;
}

/*
 *  stress_bpf
 *	stress by heavy socket I/O
 */
static int stress_bpf(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
	double rate;
	int version;
	struct bpf_insn *insns;
	size_t insns_max = DEFAULT_BPF_PROG_SIZE;
	size_t insns_sz;
	const size_t unique_insns_sz = sizeof(*unique_insns) * MAX_UNIQUE_INSNS;
	const size_t hash_table_sz = sizeof(stress_bpf_insn_node_t *) * BPF_HASH_TABLE_SIZE;

	unique_insns_count = 0;

	version = stress_bpf_kernel_version_binary();
	if (UNLIKELY(version < 0)) {
		pr_inf_skip("%s: failed to determine kernel version, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}

	if (!stress_setting_get("bpf-max", &insns_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			insns_max = MAX_BPF_PROG_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			insns_max = MIN_BPF_PROG_SIZE;
	}
	insns_sz = sizeof(*insns) * insns_max;
	insns = (struct bpf_insn *)stress_mmap_populate(NULL,
				insns_sz, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (insns == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes for BPF instructions, "
			"errno=%d (%s), skipping stressor\n",
			args->name, insns_sz, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	unique_insns = (stress_bpf_insn_node_t *)stress_mmap_populate(NULL,
				unique_insns_sz, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (unique_insns == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes for unique BPF instructions, "
			"errno=%d (%s), skipping stressor\n",
			args->name, unique_insns_sz, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto unmap_insns;
	}

	stress_bpf_insn_hash_table = (stress_bpf_insn_node_t **)stress_mmap_populate(NULL,
				hash_table_sz, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (stress_bpf_insn_hash_table == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes for hash table, "
			"errno=%d (%s), skipping stressor\n",
			args->name, unique_insns_sz, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto unmap_unique_insns;
	}

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	stress_bpf_size_max = 0;
	stress_bpf_duration = 0.0;
	stress_bpf_insns = 0.0;

	do {
		if (UNLIKELY(stress_bpf_push_op(args, insns, insns_max, version, 0, 0) < 0)) {
			rc = EXIT_NO_RESOURCE;
			break;
		}
	} while (stress_continue(args));

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	rate = (stress_bpf_duration > 0.) ? stress_bpf_insns / stress_bpf_duration : 0.0;
	stress_metrics_set(args, "loaded/verifed BPF instructions per second", rate, STRESS_METRIC_GEOMETRIC_MEAN);
	stress_metrics_set(args, "maximum BPF code instructions", (double)stress_bpf_size_max, STRESS_METRIC_MAXIMUM);
	stress_metrics_set(args, "unique BPF instructions used", (double)unique_insns_count, STRESS_METRIC_MAXIMUM);

	(void)munmap((void *)stress_bpf_insn_hash_table, hash_table_sz);
unmap_unique_insns:
	(void)munmap((void *)unique_insns, unique_insns_sz);
unmap_insns:
	(void)munmap((void *)insns, insns_sz);

	return rc;
}

const stressor_info_t stress_bpf_info = {
	.stressor = stress_bpf,
	.classifier = CLASS_NETWORK | CLASS_OS | CLASS_IPC,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.opts = opts,
	.supported = stress_bpf_supported,
	.exercises = exercises,
};

#else

const stressor_info_t stress_bpf_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_NONE,
	.help = help,
	.opts = opts,
	.supported = stress_bpf_supported,
	.exercises = exercises,
	.unimplemented_reason = "built without linux/bpf.h or bpf()"

};

#endif
