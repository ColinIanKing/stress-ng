/*
 * Copyright (C) 2013-2014 Canonical, Ltd.
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
#include <sys/mman.h>
#include "stress-ng.h"

/*
 *  stress_vm()
 *	stress virtual memory
 */
int stress_vm(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint8_t *buf = NULL;
	uint8_t	val = 0;
	size_t	i;
	const bool keep = (opt_flags & OPT_FLAGS_VM_KEEP);

	(void)instance;

	do {
		const uint8_t gray_code = (val >> 1) ^ val;
		val++;

		if (!keep || (keep && buf == NULL)) {
			buf = mmap(NULL, opt_vm_bytes, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | opt_vm_flags, -1, 0);
			if (buf == MAP_FAILED) {
				pr_failed_dbg(name, "mmap");
				continue;	/* Try again */
			}
		}

		for (i = 0; i < opt_vm_bytes; i += opt_vm_stride) {
			*(buf + i) = gray_code;
			if (!opt_do_run)
				goto unmap_cont;
		}

		if (opt_vm_hang == 0) {
			for (;;)
				(void)sleep(3600);
		} else if (opt_vm_hang != DEFAULT_VM_HANG) {
			(void)sleep((int)opt_vm_hang);
		}

		for (i = 0; i < opt_vm_bytes; i += opt_vm_stride) {
			if (*(buf + i) != gray_code) {
				if (opt_flags & OPT_FLAGS_VERIFY) {
					pr_fail(stderr, "%s: detected memory error, offset : %zd, got: %x\n",
						name, i, *(buf + i));
					break;
				} else {
					pr_err(stderr, "%s: detected memory error, offset : %zd, got: %x\n",
						name, i, *(buf + i));
					(void)munmap(buf, opt_vm_bytes);
					return EXIT_FAILURE;
				}
			}
			if (!opt_do_run)
				goto unmap_cont;
		}

unmap_cont:
		if (!keep)
			(void)munmap(buf, opt_vm_bytes);

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	if (keep)
		(void)munmap(buf, opt_vm_bytes);

	return EXIT_SUCCESS;
}
