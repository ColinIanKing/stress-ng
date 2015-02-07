/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>

#include "stress-ng.h"

#define KEY_GET_RETRIES		(40)

static size_t opt_shm_sysv_bytes = DEFAULT_SHM_SYSV_BYTES;
static size_t opt_shm_sysv_segments = DEFAULT_SHM_SYSV_SEGMENTS;


void stress_set_shm_sysv_bytes(const char *optarg)
{
	opt_shm_sysv_bytes = (size_t)get_uint64_byte(optarg);
	check_range("shm-sysv-bytes", opt_shm_sysv_bytes,
		MIN_SHM_SYSV_BYTES, MAX_SHM_SYSV_BYTES);
}

void stress_set_shm_sysv_segments(const char *optarg)
{
	opt_shm_sysv_segments = (size_t)get_uint64_byte(optarg);
	check_range("shm-sysv-segments", opt_shm_sysv_segments,
		MIN_SHM_SYSV_SEGMENTS, MAX_SHM_SYSV_SEGMENTS);
}

/*
 *  stress_shm_sysv_check()
 *	simple check if shared memory is sane
 */
static int stress_shm_sysv_check(uint8_t *buf, const size_t sz)
{
	uint8_t *ptr, *end = buf + sz;
	uint8_t val;

	memset(buf, 0xa5, sz);

	for (val = 0, ptr = buf; ptr < end; ptr += 4096, val++) {
		*ptr = val;
	}

	for (val = 0, ptr = buf; ptr < end; ptr += 4096, val++) {
		if (*ptr != val)
			return -1;

	}
	return 0;
}

/*
 *  stress_shm_sysv()
 *	stress SYSTEM V shared memory
 */
int stress_shm_sysv(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const size_t page_size = stress_get_pagesize();
	size_t sz = opt_shm_sysv_bytes & ~(page_size - 1);
	const size_t orig_sz = sz;
	void *addrs[opt_shm_sysv_segments];
	key_t keys[opt_shm_sysv_segments];
	int shm_ids[opt_shm_sysv_segments];
	int rc = EXIT_SUCCESS;
	bool ok = true;

	(void)instance;

	memset(addrs, 0, sizeof(addrs));
	memset(keys, 0, sizeof(keys));
	memset(shm_ids, 0, sizeof(shm_ids));

	/* Make sure this is killable by OOM killer */
	set_oom_adjustment(name, true);

	do {
		ssize_t i;

		for (i = 0; i < (ssize_t)opt_shm_sysv_segments; i++) {
			int shm_id, count = 0;
			void *addr;
			key_t key;

			if (!opt_do_run)
				goto reap;

			for (count = 0; count < KEY_GET_RETRIES; count++) {
				/* Get a unique key */
				bool unique = true;

				do {
					ssize_t j;
			
					if (!opt_do_run)
						goto reap;

					/* Get a unique random key */
					key = (key_t)(mwc() & 0xffff);
					for (j = 0; j < i - 1; j++) {
						if (key == keys[j]) {
							unique = false;
							break;
						}
					}
				} while (!unique);

				shm_id = shmget(key, sz,  IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
				if (shm_id >= 0)
					break;
				if (errno == EINTR)
					goto reap;
				if ((errno == EINVAL) || (errno == ENOMEM)) {
					/* On some systems we may need to reduce the size */
					if (sz > page_size)
						sz = sz / 2;
				}
			}
			if (shm_id < 0) {
				ok = false;
				pr_fail(stderr, "%s: shmget failed: errno=%d (%s)\n",
					name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto reap;
				
			}
			addr = shmat(shm_id, NULL, 0);
			if (addr == (char *) -1) {
				ok = false;
				pr_fail(stderr, "%s: shmat failed: errno=%d (%s)\n",
					name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto reap;
			}
			addrs[i] = addr;
			shm_ids[i] = shm_id;
			keys[i] = key;

			if (!opt_do_run)
				goto reap;
			(void)mincore_touch_pages(addr, sz);

			if (!opt_do_run)
				goto reap;
			(void)madvise_random(addr, sz);

			if (!opt_do_run)
				goto reap;
			if (stress_shm_sysv_check(addr, sz) < 0) {
				ok = false;
				pr_fail(stderr, "%s: memory check failed\n", name);
				rc = EXIT_FAILURE;
				goto reap;
			}
			(*counter)++;
		}
reap:
		for (i = 0; i < (ssize_t)opt_shm_sysv_segments; i++) {
			if (addrs[i]) {
				if (shmdt(addrs[i]) < 0) {
					pr_fail(stderr, "%s: shmdt failed: errno=%d (%s)\n",
						name, errno, strerror(errno));
				}
			}
			if (shm_ids[i] >= 0) {
				if (shmctl(shm_ids[i], IPC_RMID, NULL) < 0) {
					if (errno != EIDRM)
						pr_fail(stderr, "%s: shmctl failed: errno=%d (%s)\n",
							name, errno, strerror(errno));
				}
			}
			addrs[i] = NULL;
			shm_ids[i] = 0;
			keys[i] = 0;
		}
	} while (ok && opt_do_run && (!max_ops || *counter < max_ops));

	if (orig_sz != sz)
		pr_dbg(stderr, "%s: reduced shared memory size from %zu to %zu bytes\n",
			name, orig_sz, sz);
	return rc;
}
