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
#include "core-arch.h"
#include "core-asm-ret.h"
#include "core-builtin.h"
#include "core-killpid.h"
#include "core-mmap.h"

static const stress_help_t help[] = {
	{ NULL,	"easy-opcode N",	"start N workers exercising random easy opcodes" },
	{ NULL,	"easy-opcode-ops N",	"stop after N easy opcode bogo operations" },
	{ NULL, NULL,		   	NULL }
};

typedef struct stress_easy_opcode {
	const size_t  len;		/* opcode length in bytes */
	const uint8_t opcodes[4];	/* max 4 opcodes */
} stress_easy_opcode_t;

#if defined(STRESS_ARCH_ALPHA) &&	\
    defined(HAVE_MPROTECT)
#define HAVE_EASY_OPCODES
static const stress_easy_opcode_t easy_opcodes[] = {
	{ 4, { 0x1f, 0x04, 0xff, 0x47 } }, /* nop */
	{ 4, { 0x01, 0x04, 0xe1, 0x47 } }, /* mov $1,$1 */
	{ 4, { 0x02, 0x04, 0xe2, 0x47 } }, /* mov $2,$2 */
	{ 4, { 0x03, 0x04, 0xe3, 0x47 } }, /* mov $3,$3 */
	{ 4, { 0x04, 0x04, 0xe4, 0x47 } }, /* mov $4,$4 */
	{ 4, { 0x05, 0x04, 0xe5, 0x47 } }, /* mov $5,$5 */
	{ 4, { 0x06, 0x04, 0xe6, 0x47 } }, /* mov $6,$6 */
	{ 4, { 0x07, 0x04, 0xe7, 0x47 } }, /* mov $7,$7 */
	{ 4, { 0x08, 0x04, 0xe8, 0x47 } }, /* mov $8,$8 */
};
#elif defined(STRESS_ARCH_ARM) && 	\
      defined(__aarch64__) &&		\
      defined(HAVE_MPROTECT)
#define HAVE_EASY_OPCODES
static const stress_easy_opcode_t easy_opcodes[] = {
	{ 4, { 0x1f, 0x20, 0x03, 0xd5 } }, /* nop */
	{ 4, { 0x1f, 0x00, 0x00, 0xeb } }, /* cmp x0,x0 */
	{ 4, { 0x3f, 0x00, 0x01, 0xeb } }, /* cmp x1,x1 */
	{ 4, { 0x5f, 0x00, 0x02, 0xeb } }, /* cmp x2,x2 */
	{ 4, { 0x7f, 0x00, 0x03, 0xeb } }, /* cmp x3,x3 */
	{ 4, { 0x9f, 0x00, 0x04, 0xeb } }, /* cmp x4,x4 */
	{ 4, { 0xbf, 0x00, 0x05, 0xeb } }, /* cmp x5,x5 */
	{ 4, { 0xdf, 0x00, 0x06, 0xeb } }, /* cmp x6,x6 */
	{ 4, { 0xff, 0x00, 0x07, 0xeb } }, /* cmp x7,x7 */
};
#elif defined(STRESS_ARCH_HPPA) &&	\
      defined(HAVE_MPROTECT)
#define HAVE_EASY_OPCODES
static const stress_easy_opcode_t easy_opcodes[] = {
	{ 4, { 0x08, 0x00, 0x02, 0x40 } }, /* nop */
	{ 4, { 0x08, 0x08, 0x02, 0x48 } }, /* copy r8,r8 */
	{ 4, { 0x08, 0x09, 0x02, 0x49 } }, /* copy r8,r8 */
	{ 4, { 0x08, 0x0a, 0x02, 0x4a } }, /* copy r8,r8 */
	{ 4, { 0x08, 0x0b, 0x02, 0x4b } }, /* copy r8,r8 */
	{ 4, { 0x08, 0x0c, 0x02, 0x4c } }, /* copy r8,r8 */
	{ 4, { 0x08, 0x0d, 0x02, 0x4d } }, /* copy r8,r8 */
	{ 4, { 0x08, 0x0e, 0x02, 0x4e } }, /* copy r8,r8 */
	{ 4, { 0x08, 0x0f, 0x02, 0x4f } }, /* copy r8,r8 */
};
#elif defined(STRESS_ARCH_LOONG64) &&	\
      defined(STRESS_ARCH_LE) &&	\
      defined(HAVE_MPROTECT)
#define HAVE_EASY_OPCODES
static const stress_easy_opcode_t easy_opcodes[] = {
	{ 4, { 0x00, 0x00, 0x40, 0x03 } }, /* nop */
	{ 4, { 0x8c, 0x01, 0x15, 0x00 } }, /* move $t0,$t0 */
	{ 4, { 0xad, 0x01, 0x15, 0x00 } }, /* move $t1,$t1 */
	{ 4, { 0xce, 0x01, 0x15, 0x00 } }, /* move $t2,$t2 */
	{ 4, { 0xef, 0x01, 0x15, 0x00 } }, /* move $t3,$t3 */
	{ 4, { 0x10, 0x02, 0x15, 0x00 } }, /* move $t4,$t4 */
	{ 4, { 0x31, 0x02, 0x15, 0x00 } }, /* move $t5,$t5 */
	{ 4, { 0x52, 0x02, 0x15, 0x00 } }, /* move $t6,$t6 */
	{ 4, { 0x73, 0x02, 0x15, 0x00 } }, /* move $t7,$t7 */
};
#elif defined(STRESS_ARCH_LOONG64) &&	\
      defined(STRESS_ARCH_BE) &&	\
      defined(HAVE_MPROTECT)
#define HAVE_EASY_OPCODES
static const stress_easy_opcode_t easy_opcodes[] = {
	{ 4, { 0x03, 0x40, 0x00, 0x00 } }, /* nop */
	{ 4, { 0x00, 0x15, 0x01, 0x8c } }, /* move $t0,$t0 */
	{ 4, { 0x00, 0x15, 0x01, 0xad } }, /* move $t1,$t1 */
	{ 4, { 0x00, 0x15, 0x01, 0xce } }, /* move $t2,$t2 */
	{ 4, { 0x00, 0x15, 0x01, 0xef } }, /* move $t3,$t3 */
	{ 4, { 0x00, 0x15, 0x02, 0x10 } }, /* move $t4,$t4 */
	{ 4, { 0x00, 0x15, 0x02, 0x31 } }, /* move $t4,$t4 */
	{ 4, { 0x00, 0x15, 0x02, 0x52 } }, /* move $t4,$t4 */
	{ 4, { 0x00, 0x15, 0x02, 0x73 } }, /* move $t4,$t4 */
};
#elif defined(STRESS_ARCH_M68K) &&	\
      defined(HAVE_MPROTECT)
#define HAVE_EASY_OPCODES
static const stress_easy_opcode_t easy_opcodes[] = {
	{ 2, { 0x4e, 0x71 } }, /* nop */
	{ 2, { 0x20, 0x00 } }, /* movel %d0,%d0 */
	{ 2, { 0x22, 0x01 } }, /* movel %d0,%d0 */
	{ 2, { 0x24, 0x02 } }, /* movel %d0,%d0 */
	{ 2, { 0x26, 0x03 } }, /* movel %d0,%d0 */
	{ 2, { 0x28, 0x04 } }, /* movel %d0,%d0 */
	{ 2, { 0x2a, 0x05 } }, /* movel %d0,%d0 */
	{ 2, { 0x2c, 0x06 } }, /* movel %d0,%d0 */
	{ 2, { 0x2e, 0x07 } }, /* movel %d0,%d0 */
        { 2, { 0x50, 0xc0 } }, /* st %d0 */
        { 2, { 0x51, 0xc0 } }, /* sf %d0 */
        { 2, { 0x52, 0xc0 } }, /* shi %d0 */
        { 2, { 0x53, 0xc0 } }, /* sls %d0 */
        { 2, { 0x54, 0xc0 } }, /* scc %d0 */
        { 2, { 0x55, 0xc0 } }, /* scs %d0 */
        { 2, { 0x56, 0xc0 } }, /* sne %d0 */
        { 2, { 0x57, 0xc0 } }, /* seq %d0 */
        { 2, { 0x58, 0xc0 } }, /* svc %d0 */
        { 2, { 0x59, 0xc0 } }, /* svs %d0 */
        { 2, { 0x5a, 0xc0 } }, /* spl %d0 */
        { 2, { 0x5b, 0xc0 } }, /* smi %d0 */
        { 2, { 0x5c, 0xc0 } }, /* sge %d0 */
        { 2, { 0x5d, 0xc0 } }, /* slt %d0 */
        { 2, { 0x5e, 0xc0 } }, /* sgt %d0 */
        { 2, { 0x5f, 0xc0 } }, /* sle %d0 */
};
#elif defined(STRESS_ARCH_MIPS) &&	\
      defined(STRESS_ARCH_LE) &&	\
      defined(HAVE_MPROTECT)
#define HAVE_EASY_OPCODES
static const stress_easy_opcode_t easy_opcodes[] = {
	{ 4, { 0x00, 0x00, 0x00, 0x00 } }, /* nop */
	{ 4, { 0x25, 0x40, 0x00, 0x01 } }, /* move $8,$8 */
	{ 4, { 0x25, 0x48, 0x20, 0x01 } }, /* move $9,$9 */
	{ 4, { 0x25, 0x50, 0x40, 0x01 } }, /* move $10,$10 */
	{ 4, { 0x25, 0x58, 0x60, 0x01 } }, /* move $11,$11 */
	{ 4, { 0x25, 0x60, 0x80, 0x01 } }, /* move $12,$12 */
	{ 4, { 0x25, 0x68, 0xa0, 0x01 } }, /* move $13,$13 */
	{ 4, { 0x25, 0x70, 0xc0, 0x01 } }, /* move $14,$14 */
	{ 4, { 0x25, 0x78, 0xe0, 0x01 } }, /* move $15,$15 */
};
#elif defined(STRESS_ARCH_MIPS) &&	\
      defined(STRESS_ARC_BE) &&		\
      defined(HAVE_MPROTECT)
#define HAVE_EASY_OPCODES
static const stress_easy_opcode_t easy_opcodes[] = {
	{ 4, { 0x00, 0x00, 0x00, 0x00 } }, /* nop */
	{ 4, { 0x01, 0x00, 0x40, 0x25 } }, /* move $8,$8 */
	{ 4, { 0x01, 0x20, 0x48, 0x25 } }, /* move $9,$9 */
	{ 4, { 0x01, 0x40, 0x50, 0x25 } }, /* move $10,$10 */
	{ 4, { 0x01, 0x60, 0x58, 0x25 } }, /* move $11,$11 */
	{ 4, { 0x01, 0x80, 0x60, 0x25 } }, /* move $12,$12 */
	{ 4, { 0x01, 0xa0, 0x68, 0x25 } }, /* move $13,$13 */
	{ 4, { 0x01, 0xc0, 0x70, 0x25 } }, /* move $14,$14 */
	{ 4, { 0x01, 0xe0, 0x78, 0x25 } }, /* move $15,$15 */
};
#elif defined(STRESS_ARCH_PPC64) &&	\
      defined(STRESS_ARCH_LE) &&	\
      defined(HAVE_MPROTECT)
#define HAVE_EASY_OPCODES
static const stress_easy_opcode_t easy_opcodes[] = {
	{ 4, { 0x00, 0x00, 0x00, 0x60  } }, /* nop */
	{ 4, { 0x78, 0x1b, 0x63, 0x07c } }, /* mr %r3,%r3 */
	{ 4, { 0x78, 0x23, 0x84, 0x07c } }, /* mr %r4,%r4 */
	{ 4, { 0x78, 0x2b, 0xa5, 0x07c } }, /* mr %r5,%r5 */
	{ 4, { 0x78, 0x33, 0xc6, 0x07c } }, /* mr %r6,%r6 */
	{ 4, { 0x78, 0x3b, 0xe7, 0x07c } }, /* mr %r7,%r7 */
	{ 4, { 0x78, 0x43, 0x08, 0x07d } }, /* mr %r8,%r8 */
	{ 4, { 0x78, 0x4b, 0x29, 0x07d } }, /* mr %r9,%r9 */
	{ 4, { 0x78, 0x53, 0x4a, 0x07d } }, /* mr %r10,%r10 */
};
#elif defined(STRESS_ARCH_RISCV) &&	\
      defined(HAVE_MPROTECT)
#define HAVE_EASY_OPCODES
static const stress_easy_opcode_t easy_opcodes[] = {
	{ 2, { 0x01, 0x00 } }, /* addi x0,x0,0 aka nop */
	{ 2, { 0x86, 0x80 } }, /* addi x1,x1,0 */
	{ 2, { 0x0a, 0x81 } }, /* addi x2,x2,0 */
	{ 2, { 0x8e, 0x81 } }, /* addi x3,x3,0 */
	{ 2, { 0x12, 0x82 } }, /* addi x4,x4,0 */
	{ 2, { 0x96, 0x82 } }, /* addi x5,x5,0 */
	{ 2, { 0x1a, 0x83 } }, /* addi x6,x6,0 */
	{ 2, { 0x9e, 0x83 } }, /* addi x7,x7,0 */
};
#elif defined(STRESS_ARCH_S390) &&	\
      defined(HAVE_MPROTECT)
#define HAVE_EASY_OPCODES
static const stress_easy_opcode_t easy_opcodes[] = {
	{ 4, { 0x47, 0x00, 0x00, 0x00 } }, /* nop */
	{ 4, { 0xb9, 0x04, 0x00, 0x22 } }, /* lgr %r2, %r2 */
	{ 4, { 0xb9, 0x04, 0x00, 0x33 } }, /* lgr %r3, %r3 */
	{ 4, { 0xb9, 0x04, 0x00, 0x44 } }, /* lgr %r4, %r4 */
	{ 4, { 0xb9, 0x04, 0x00, 0x55 } }, /* lgr %r5, %r5 */
	{ 4, { 0xb9, 0x04, 0x00, 0x66 } }, /* lgr %r6, %r6 */
	{ 4, { 0xb9, 0x04, 0x00, 0x77 } }, /* lgr %r7, %r7 */
	{ 4, { 0xb9, 0x04, 0x00, 0x88 } }, /* lgr %r8, %r8 */
	{ 4, { 0xb9, 0x04, 0x00, 0x99 } }, /* lgr %r9, %r9 */
};
#elif defined(STRESS_ARCH_SH4) &&	\
      defined(HAVE_MPROTECT)
#define HAVE_EASY_OPCODES
static const stress_easy_opcode_t easy_opcodes[] = {
	{ 2, { 0x09, 0x00 } }, /* nop */
	{ 2, { 0x83, 0x68 } }, /* mov r8,r8 */
	{ 2, { 0x93, 0x69 } }, /* mov r9,r9 */
	{ 2, { 0xa3, 0x6a } }, /* mov r10,r10 */
	{ 2, { 0xb3, 0x6b } }, /* mov r11,r11 */
	{ 2, { 0xc3, 0x6c } }, /* mov r12,r12 */
	{ 2, { 0xd3, 0x6d } }, /* mov r13,r13 */
	{ 2, { 0xe3, 0x6e } }, /* mov r14,r14 */
	{ 2, { 0xf3, 0x6f } }, /* mov r15,r15 */
};
#elif defined(STRESS_ARCH_SPARC) &&	\
      defined(HAVE_MPROTECT)
#define HAVE_EASY_OPCODES
static const stress_easy_opcode_t easy_opcodes[] = {
	{ 4, { 0x01, 0x00, 0x00, 0x00 } }, /* nop */
	{ 4, { 0xa0, 0x10, 0x00, 0x10 } }, /* mov %l0, %l0 */
	{ 4, { 0xa2, 0x10, 0x00, 0x11 } }, /* mov %l1, %l1 */
	{ 4, { 0xa4, 0x10, 0x00, 0x12 } }, /* mov %l2, %l2 */
	{ 4, { 0xa6, 0x10, 0x00, 0x13 } }, /* mov %l3, %l3 */
	{ 4, { 0xa8, 0x10, 0x00, 0x14 } }, /* mov %l4, %l4 */
	{ 4, { 0xaa, 0x10, 0x00, 0x15 } }, /* mov %l5, %l5 */
	{ 4, { 0xac, 0x10, 0x00, 0x16 } }, /* mov %l6, %l6 */
	{ 4, { 0xae, 0x10, 0x00, 0x17 } }, /* mov %l7, %l7 */
};
#elif defined(STRESS_ARCH_X86) &&	\
      defined(HAVE_MPROTECT)
#define HAVE_EASY_OPCODES
static const stress_easy_opcode_t easy_opcodes[] = {
	{ 1, { 0x90 } }, /* nop */
	{ 1, { 0xf5 } }, /* cmc */
	{ 1, { 0xf8 } }, /* clc */
	{ 1, { 0xf9 } }, /* slc */
	{ 1, { 0xfc } }, /* cld */
	{ 1, { 0xfd } }, /* std */
};
#endif

#if defined(HAVE_EASY_OPCODES)

#define PAGES		(64)

typedef void(*stress_easy_opcode_func)(const size_t page_size, void *ops_begin,
				  const void *ops_end, const volatile uint64_t *op);

typedef struct  {
	volatile uint64_t bogo_ops;
	size_t ops;
} stress_easy_opcode_state_t;


static inline size_t OPTIMIZE3 stress_easy_opcode_fill(void *ops_begin, size_t size)
{
	uint8_t *ptr = (uint8_t *)ops_begin, *ptr_end;
	size_t ops = 0, i, max_op_len = stress_ret_opcode.len;

	for (i = 0; i < SIZEOF_ARRAY(easy_opcodes); i++) {
		if (max_op_len < easy_opcodes[i].len)
			max_op_len = easy_opcodes[i].len;
	}

	ptr_end = ptr + size - max_op_len;
	while (ptr < (uint8_t *)ptr_end) {
		register const size_t idx = stress_mwc8modn((uint8_t)SIZEOF_ARRAY(easy_opcodes));

		(void)shim_memcpy(ptr, &easy_opcodes[idx].opcodes, easy_opcodes[idx].len);
		ptr += easy_opcodes[idx].len;
		ops++;
	}
	(void)shim_memcpy(ptr, stress_ret_opcode.opcodes, stress_ret_opcode.len);
	ops++;

	return ops;
}

/*
 *  stress_easy_opcode
 *	stress with random opcodes
 */
static int stress_easy_opcode(stress_args_t *args)
{
	const size_t page_size = args->page_size;
	int rc;
	double rate, t, duration;
	void *opcodes;
	stress_easy_opcode_state_t *state;

	if (stress_asm_ret_supported(args->name) < 0)
		return EXIT_NO_RESOURCE;

	state = (stress_easy_opcode_state_t *)
		stress_mmap_populate(NULL, sizeof(*state),
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (state == MAP_FAILED) {
		pr_inf_skip("%s: mmap of %zu bytes failed%s, errno=%d (%s) "
			"skipping stressor\n",
			args->name, args->page_size, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(state, sizeof(*state), "state");

	state->bogo_ops = 0;
	state->ops = 0;

	opcodes = (void *)stress_mmap_populate(NULL, page_size * (2 + PAGES),
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (opcodes == MAP_FAILED) {
		pr_fail("%s: mmap of %zu bytes failed%s, errno=%d (%s)\n",
			args->name, page_size * (2 + PAGES),
			stress_get_memfree_str(),
			errno, strerror(errno));
		(void)munmap((void *)state, sizeof(*state));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(opcodes, page_size * PAGES, "opcodes");
	/* Force pages resident */
	(void)shim_memset(opcodes, 0x00, page_size * PAGES);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t = stress_time_now();
	do {
		pid_t pid;

		/*
		 *  Force a new random value so that child always
		 *  gets a different random value on each fork
		 */
		(void)stress_mwc32();
again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (UNLIKELY(!stress_continue(args)))
				goto finish;
			pr_fail("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			goto err;
		} else if (pid == 0) {
			const size_t ops_size = page_size * PAGES;
			void *ops_begin = (uint8_t *)((uintptr_t)opcodes + page_size);
			void *ops_end = (void *)((uint8_t *)ops_begin + ops_size);
			static volatile uint64_t bogo_ops = 0;

			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			(void)sched_settings_apply(true);

#if defined(MADV_HUGEPAGE)
			(void)shim_madvise((void*)opcodes, page_size * (PAGES + 2), MADV_HUGEPAGE);
#endif
			(void)mprotect((void *)opcodes, ops_size, PROT_NONE);
			(void)mprotect((void *)ops_end, page_size, PROT_NONE);

			(void)mprotect((void *)ops_begin, ops_size, PROT_WRITE);
			/* Populate with opcodes */
			state->ops = (double)stress_easy_opcode_fill(ops_begin, ops_size);
			/* Make read-only executable and force I$ flush */
			(void)mprotect((void *)ops_begin, ops_size, PROT_READ | PROT_EXEC);
			shim_flush_icache((char *)ops_begin, (char *)ops_end);

			stress_parent_died_alarm();

			for (;;) {
				((void (*)(void))(ops_begin))();
#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_EASY_OPCODES)
				__asm__ __volatile__("cld;\n");
#endif

				bogo_ops++;

				if (UNLIKELY(!stress_continue_flag()))
					break;
				if (UNLIKELY((args->bogo.max_ops > 0) && state->bogo_ops >= args->bogo.max_ops))
					break;
			}
			state->bogo_ops = bogo_ops;
			_exit(0);
		} else if (pid > 0) {
			pid_t ret;
			int status;

			ret = shim_waitpid(pid, &status, 0);
			if (ret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid() on PID %" PRIdMAX " failed, errno=%d (%s)\n",
						args->name, (intmax_t)pid, errno, strerror(errno));
				(void)stress_kill_pid_wait(pid, NULL);
			}
			stress_bogo_set(args, state->bogo_ops);
		}
	} while (stress_continue(args));

finish:
	duration = stress_time_now() - t;
	rc = EXIT_SUCCESS;

	rate = (duration > 0.0) ? (double)state->ops * (double)state->bogo_ops / duration : 0.0;
	stress_metrics_set(args, 0, "easy opcodes exercised per sec", rate, STRESS_METRIC_HARMONIC_MEAN);
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)opcodes, page_size * (2 + PAGES));
	(void)munmap((void *)state, sizeof(*state));
	return rc;
}

const stressor_info_t stress_easy_opcode_info = {
	.stressor = stress_easy_opcode,
	.classifier = CLASS_CPU,
	.help = help
};
#else

const stressor_info_t stress_easy_opcode_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU,
	.help = help,
	.unimplemented_reason = "built without mprotect()"
};
#endif
