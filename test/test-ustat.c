/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#define  _GNU_SOURCE

#if defined(__gnu_hurd__) || defined(__aarch64__)
#error ustat is not implemented and will always fail on this system
#endif

#include <sys/types.h>
#include <unistd.h>
#include <ustat.h>
#if defined(__linux__)
#include <sys/sysmacros.h>
#endif

int main(void)
{
	dev_t dev = makedev(8, 1);
	struct ustat ubuf;

	return ustat(dev, &ubuf);
}
