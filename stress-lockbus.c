/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"lockbus N",	 "start N workers locking a memory increment" },
	{ NULL,	"lockbus-ops N", "stop after N lockbus bogo operations" },
	{ NULL, NULL,		 NULL }
};

#if (((defined(__GNUC__) || defined(__clang__)) && defined(STRESS_X86)) || \
    (defined(__GNUC__) && NEED_GNUC(4,7,0) && defined(STRESS_ARM)))

#define BUFFER_SIZE	(1024 * 1024 * 16)
#define CHUNK_SIZE	(64 * 4)

#if defined(__GNUC__) && NEED_GNUC(4,7,0)
#define LOCK(ptr) __atomic_add_fetch(ptr, inc, __ATOMIC_SEQ_CST);

#else
#define LOCK(ptr) asm volatile("lock addl %1,%0" : "+m" (*ptr) : "ir" (inc)); 

#endif

#define LOCK_AND_INC(ptr, inc)	\
	LOCK(ptr);		\
	ptr++;

#define LOCK_AND_INCx8(ptr, inc)	\
	LOCK_AND_INC(ptr, inc)		\
	LOCK_AND_INC(ptr, inc)		\
	LOCK_AND_INC(ptr, inc)		\
	LOCK_AND_INC(ptr, inc)		\
	LOCK_AND_INC(ptr, inc)		\
	LOCK_AND_INC(ptr, inc)		\
	LOCK_AND_INC(ptr, inc)		\
	LOCK_AND_INC(ptr, inc)

#define LOCKx8(ptr)			\
	LOCK(ptr)			\
	LOCK(ptr)			\
	LOCK(ptr)			\
	LOCK(ptr)			\
	LOCK(ptr)			\
	LOCK(ptr)			\
	LOCK(ptr)			\
	LOCK(ptr)

#if defined(STRESS_X86)
static sigjmp_buf jmp_env;
static bool do_splitlock;

static void MLOCKED_TEXT stress_sigbus_handler(int signum)
{
	(void)signum;

	do_splitlock = false;

	siglongjmp(jmp_env, 1);
}
#endif

/*
 *  stress_lockbus()
 *      stress memory with lock and increment
 */
static int stress_lockbus(const args_t *args)
{
	uint32_t *buffer;
	int flags = MAP_ANONYMOUS | MAP_SHARED;
#if defined(STRESS_X86)
	uint32_t *splitlock_ptr1, *splitlock_ptr2;

	if (stress_sighandler(args->name, SIGBUS, stress_sigbus_handler, NULL) < 0)
		return EXIT_FAILURE;
#endif

#if defined(MAP_POPULATE)
	flags |= MAP_POPULATE;
#endif
	buffer = (uint32_t*)mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, flags, -1, 0);
	if (buffer == MAP_FAILED) {
		int rc = exit_status(errno);
		pr_err("%s: mmap failed\n", args->name);
		return rc;
	}

#if defined(STRESS_X86)
	/* Split lock on a page boundary */
	splitlock_ptr1 = (uint32_t *)(((uint8_t *)buffer) + args->page_size - (sizeof(uint32_t) >> 1));
	/* Split lock on a cache boundary */
	splitlock_ptr2 = (uint32_t *)(((uint8_t *)buffer) + 64 - (sizeof(uint32_t) >> 1));
	do_splitlock = true;
	if (sigsetjmp(jmp_env, 1) && !keep_stressing())
		goto done;
#endif

	do {
		uint32_t *ptr0 = buffer + ((mwc32() % (BUFFER_SIZE - CHUNK_SIZE)) >> 2);
#if defined(STRESS_X86)
		uint32_t *ptr1 = do_splitlock ? splitlock_ptr1 : ptr0;
		uint32_t *ptr2 = do_splitlock ? splitlock_ptr2 : ptr0;
#else
		uint32_t *ptr1 = ptr0;
		uint32_t *ptr2 = ptr0;
#endif
		const uint32_t inc = 1;

		LOCK_AND_INCx8(ptr0, inc);
		LOCKx8(ptr1);
		LOCKx8(ptr2);
		LOCK_AND_INCx8(ptr0, inc);
		LOCKx8(ptr1);
		LOCKx8(ptr2);
		LOCK_AND_INCx8(ptr0, inc);
		LOCKx8(ptr1);
		LOCKx8(ptr2);
		LOCK_AND_INCx8(ptr0, inc);
		LOCKx8(ptr1);
		LOCKx8(ptr2);

		inc_counter(args);
	} while (keep_stressing());

#if defined(STRESS_X86)
done:
#endif
	(void)munmap((void *)buffer, BUFFER_SIZE);

	return EXIT_SUCCESS;
}

stressor_info_t stress_lockbus_info = {
	.stressor = stress_lockbus,
	.class = CLASS_CPU_CACHE | CLASS_MEMORY,
	.help = help
};
#else
stressor_info_t stress_lockbus_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU_CACHE | CLASS_MEMORY,
	.help = help
};
#endif
