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
#ifndef CORE_IOCTL_H
#define CORE_IOCTL_H

#include "stress-ng.h"
#include "core-helper.h"

extern WARN_UNUSED int stress_ioctl_get_check(const int fd,
	const unsigned long op, const size_t len);
extern WARN_UNUSED int stress_ioctl_termios_getset(stress_args_t *args, int fd,
	const unsigned long cmd_get, const char *cmd_get_str,
	const unsigned long cmd_set, const char *cmd_set_str);
extern int stress_termios_hangup(int fd);


#endif
