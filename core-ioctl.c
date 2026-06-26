/*
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
#include "core-ioctl.h"

#include <sys/ioctl.h>

/*
 *  stress_ioctl_get_check()
 *	check if getting an ioctl() actually changes
 *	a value. Return -1 if no change occurs, ignore
 *	failed ioctls.
 */
int stress_ioctl_get_check(const int fd, const unsigned long op, const size_t len)
{
	int8_t val8;
	int16_t val16;
	int32_t val32;
	int64_t val64;
	int ret;

	switch (len) {
	case 1:
		val8 = ~0;
		ret = ioctl(fd, op, &val8);
		if (ret < 0)
			return 0;
		if (val8 != ~0)
			return 0;
		val8 = 0;
		ret = ioctl(fd, op, &val8);
		if (ret < 0)
			return 0;
		if (val8 != 0)
			return 0;
		break;
	case 2:
		val16 = ~0;
		ret = ioctl(fd, op, &val16);
		if (ret < 0)
			return 0;
		if (val16 != ~0)
			return 0;
		val16 = 0;
		ret = ioctl(fd, op, &val16);
		if (ret < 0)
			return 0;
		if (val16 != 0)
			return 0;
		break;
	case 4:
		val32 = ~0;
		ret = ioctl(fd, op, &val32);
		if (ret < 0)
			return 0;
		if (val32 != ~0)
			return 0;
		val32 = 0;
		ret = ioctl(fd, op, &val32);
		if (ret < 0)
			return 0;
		if (val32 != 0)
			return 0;
		break;
	case 8:
		val64 = ~0;
		ret = ioctl(fd, op, &val64);
		if (ret < 0)
			return 0;
		if (val64 != ~0)
			return 0;
		val64 = 0;
		ret = ioctl(fd, op, &val64);
		if (ret < 0)
			return 0;
		if (val64 != 0)
			return 0;
		break;
	default:
		/* ignore any other size */
		return 0;
	}
	return -1;
}
