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
#include "core-builtin.h"

#include <sys/ioctl.h>

#if defined(HAVE_ASM_TERMBITS_H) &&             \
    defined(HAVE_ASM_TERMBITS_STRUCT_TERMIOS)
#include <asm/termbits.h>
#endif

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

int stress_ioctl_termios_get_str(
	stress_args_t *args,
	int fd,
	const int cmd_get,
	const char *cmd_get_str)
{
#if defined(HAVE_ASM_TERMBITS_H) &&             \
    defined(HAVE_ASM_TERMBITS_STRUCT_TERMIOS)
	struct termios_wrapper {
		struct termios ios;
		uint8_t pad[4];
	} tios;
	int ret;

	shim_memset(&tios.ios, 0x00, sizeof(tios.ios));
	shim_memset(&tios.pad, 0xff, sizeof(tios.pad));
	ret = ioctl(fd, cmd_get, &tios.ios);
	if (ret < 0) {
		if (errno == EINTR)
			return 0;
		pr_fail("%s: ioctl %s failed, errno=%d (%s)\n",
			args->name, cmd_get_str, errno, strerror(errno));
		return -1;
	}
	if ((tios.pad[0] != 0xff) ||
	    (tios.pad[1] != 0xff) ||
	    (tios.pad[2] != 0xff) ||
	    (tios.pad[3] != 0xff)) {
		pr_fail("%s: ioctl %s overran struct termios\n",
			args->name, cmd_get_str);
		return -1;
	}
#endif
	return 0;
}

int stress_ioctl_termios_getset_str(
	stress_args_t *args,
	int fd,
	const int cmd_get,
	const char *cmd_get_str,
	const int cmd_set,
	const char *cmd_set_str)
{
#if defined(HAVE_ASM_TERMBITS_H) &&             \
    defined(HAVE_ASM_TERMBITS_STRUCT_TERMIOS)
	struct termios_wrapper {
		struct termios ios;
		uint8_t pad[4];
	} tios;
	int ret;

	shim_memset(&tios.ios, 0x00, sizeof(tios.ios));
	shim_memset(&tios.pad, 0xff, sizeof(tios.pad));
	ret = ioctl(fd, cmd_get, &tios.ios);
	if (ret < 0) {
		if (errno == EINTR)
			return 0;
		pr_fail("%s: ioctl %s failed, errno=%d (%s)\n",
			args->name, cmd_get_str, errno, strerror(errno));
		return -1;
	}
	if ((tios.pad[0] != 0xff) ||
	    (tios.pad[1] != 0xff) ||
	    (tios.pad[2] != 0xff) ||
	    (tios.pad[3] != 0xff)) {
		pr_fail("%s: ioctl %s overran struct termios\n",
			args->name, cmd_get_str);
		return -1;
	}
	ret = ioctl(fd, cmd_set, &tios.ios);
	if (ret < 0) {
		if (errno == EINTR)
			return 0;
		pr_fail("%s: ioctl %s failed, errno=%d (%s)\n",
			args->name, cmd_set_str, errno, strerror(errno));
		return -1;
	}
#endif
	return 0;
}

/*
 *  stress_termios_hangup()
 *	perform hangup by setting c_flag to zero baud
 */
int stress_termios_hangup(int fd)
{
#if defined(HAVE_ASM_TERMBITS_H) &&                     \
    defined(HAVE_ASM_TERMBITS_STRUCT_TERMIOS) &&        \
    defined(TCGETS) &&                  \
    defined(TCSETS) &&                  \
    defined(CBAUD) &&                   \
    defined(B0)
	struct termios ios;

	if (ioctl(fd, TCGETS, &ios) == 0) {
		ios.c_cflag &= ~CBAUD;
		ios.c_cflag |= B0 & CBAUD;
		return ioctl(fd, TCSETS, &ios);
	}
#endif
	return -1;
}
