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

#define MAX_BPF_OPCODES		(256)

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

static uint64_t bpf_fail;
static uint64_t bpf_success;
static int bpf_size_max;

static inline int sys_bpf(
	enum bpf_cmd cmd,
	union bpf_attr *attr,
	unsigned int size)
{
	return syscall(__NR_bpf, cmd, attr, size);
}

static inline int bpf_prog_load(
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

	return sys_bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
}

static int OPTIMIZE3 stress_bpf_push_op(
	stress_args_t *args,
	struct bpf_insn *insns,
	const size_t insns_max,
	const int version,
	const size_t len,
	const int next_opcode)
{
	register int opcode = next_opcode & 0xff;
	register int i;
	struct bpf_insn saved;
	const int prog_len = len + 2;

	if (len >= insns_max - 1)
		return 0;

	saved = insns[len];

	for (i = 0; i < MAX_BPF_OPCODES; i++) {
		int fd;

		if (!stress_continue(args))
			return 0;

		insns[len].code = opcode;
		insns[len].dst_reg = 0;
		insns[len].src_reg = stress_mwc8modn(11);
		insns[len].off = 0;
		insns[len].imm = 0;

		insns[len + 1].code = BPF_JMP | BPF_EXIT;
		insns[len + 1].dst_reg = 0;
		insns[len + 1].src_reg = 0;
		insns[len + 1].off = 0;
		insns[len + 1].imm = 0;

		fd = bpf_prog_load(BPF_PROG_TYPE_KPROBE, insns, prog_len, version);
		if (fd == -1) {
			if (errno == ENOSYS) {
				pr_inf_skip("%s: bpf() system call not supported, errno=%d (%s), "
					"skipping stressor\n", args->name, errno, strerror(errno));
				return -1;
			}
			bpf_fail++;
		} else {
			if (bpf_size_max < prog_len)
				bpf_size_max = prog_len;
			(void)close(fd);

			bpf_success++;
			stress_bpf_push_op(args, insns, insns_max, version, len + 1, opcode + 1);
		}
		stress_bogo_inc(args);
		opcode = (opcode + 1) & 0xff;
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
	int major;
	int minor;
	int patch;
	int version;

	version = stress_kernel_release_get();
	if (version > 0) {
		major = (version / 10000) % 100;
		minor = (version / 100) % 100;
		patch = (version) % 100;
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
	double percent;
	int version;
	size_t insns_max = DEFAULT_BPF_PROG_SIZE;
	size_t insns_sz;
	struct bpf_insn *insns;

	version = stress_bpf_kernel_version_binary();
	if (version < 0) {
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

	insns = stress_mmap_populate(NULL, insns_sz, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (insns == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes for BPF instructions, "
			"errno=%d (%s), skipping stressor\n",
			args->name, insns_sz, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	bpf_fail = 0;
	bpf_success = 0;
	bpf_size_max = 0;

	do {
		if (stress_bpf_push_op(args, insns, insns_max, version, 0, 0) < 0) {
			rc = EXIT_NO_RESOURCE;
			break;
		}
	} while (stress_continue(args));

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)insns, insns_sz);

	percent = (bpf_fail > 0) ? (double)bpf_success * 100.0 / (double)bpf_fail : 0.0;

	stress_metrics_set(args, "% BPF program success rate", percent, STRESS_METRIC_GEOMETRIC_MEAN);
	stress_metrics_set(args, "maxiumum BPF code instructions", (double)bpf_size_max, STRESS_METRIC_MAXIMUM);

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
