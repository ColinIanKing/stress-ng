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

#if (((defined(__GNUC__) || defined(__clang__)) && defined(STRESS_X86)) || \
    (defined(__GNUC__) && NEED_GNUC(4,7,0) && defined(STRESS_ARM))) && defined(__linux__)

#define BUFFER_SIZE	(1024 * 1024 * 16)
#define CHUNK_SIZE	(64 * 4)

#if defined(__GNUC__) && NEED_GNUC(4,7,0)
#define LOCK_AND_INC(ptr, inc)					       \
	__atomic_add_fetch(ptr, inc, __ATOMIC_SEQ_CST);		       \
	ptr++;

#else
#define LOCK_AND_INC(ptr, inc)					       \
	asm volatile("lock addl %1,%0" : "+m" (*ptr) : "ir" (inc));    \
	ptr++;

#endif

#define LOCK_AND_INCx8(ptr, inc)	\
	LOCK_AND_INC(ptr, inc)		\
	LOCK_AND_INC(ptr, inc)		\
	LOCK_AND_INC(ptr, inc)		\
	LOCK_AND_INC(ptr, inc)		\
	LOCK_AND_INC(ptr, inc)		\
	LOCK_AND_INC(ptr, inc)		\
	LOCK_AND_INC(ptr, inc)		\
	LOCK_AND_INC(ptr, inc)

/*
 *  stress_lockbus()
 *      stress memory with lock and increment
 */
int stress_lockbus(const args_t *args)
{
	uint32_t *buffer;
	int flags = MAP_ANONYMOUS | MAP_SHARED;

#if defined(MAP_POPULATE)
	flags |= MAP_POPULATE;
#endif
	buffer = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, flags, -1, 0);
	if (buffer == MAP_FAILED) {
		int rc = exit_status(errno);
		pr_err("%s: mmap failed\n", args->name);
		return rc;
	}

	do {
		uint32_t *ptr = buffer + ((mwc32() % (BUFFER_SIZE - CHUNK_SIZE)) >> 2);
		const uint32_t inc = 1;

		LOCK_AND_INCx8(ptr, inc);
		LOCK_AND_INCx8(ptr, inc);
		LOCK_AND_INCx8(ptr, inc);
		LOCK_AND_INCx8(ptr, inc);
		LOCK_AND_INCx8(ptr, inc);
		LOCK_AND_INCx8(ptr, inc);
		LOCK_AND_INCx8(ptr, inc);
		LOCK_AND_INCx8(ptr, inc);

		inc_counter(args);
	} while (keep_stressing());

	(void)munmap(buffer, BUFFER_SIZE);

	return EXIT_SUCCESS;
}
#else
int stress_lockbus(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
