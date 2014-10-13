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
#include <stdint.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/types.h>

#include "stress-ng.h"

/*
 *  mwc()
 *	fast pseudo random number generator, see
 *	http://www.cse.yorku.ca/~oz/marsaglia-rng.html
 */
uint64_t mwc(void)
{
	mwc_z = 36969 * (mwc_z & 65535) + (mwc_z >> 16);
	mwc_w = 18000 * (mwc_w & 65535) + (mwc_w >> 16);
	return (mwc_z << 16) + mwc_w;
}

/*
 *  mwc_reseed()
 *	dirty mwc reseed
 */
void mwc_reseed(void)
{
	struct timeval tv;
	int i, n;

	mwc_z = 0;
	if (gettimeofday(&tv, NULL) == 0)
		mwc_z = (uint64_t)tv.tv_sec ^ (uint64_t)tv.tv_usec;
	mwc_z += ~((unsigned char *)&mwc_z - (unsigned char *)&tv);
	mwc_w = (uint64_t)getpid() ^ (uint64_t)getppid()<<12;

	n = (int)mwc_z % 1733;
	for (i = 0; i < n; i++)
		(void)mwc();
}
