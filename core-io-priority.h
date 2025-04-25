/*
 * Copyright (C) 2022-2025 Colin Ian King
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
#ifndef CORE_IO_PRIORITY_H
#define CORE_IO_PRIORITY_H

/*
 *  See ioprio_set(2) and linux/ioprio.h, glibc has no definitions
 *  for these at present. Also refer to Documentation/block/ioprio.txt
 *  in the Linux kernel source.
 */
#if !defined(IOPRIO_CLASS_RT)
#define IOPRIO_CLASS_RT         (1)
#endif
#if !defined(IOPRIO_CLASS_BE)
#define IOPRIO_CLASS_BE         (2)
#endif
#if !defined(IOPRIO_CLASS_IDLE)
#define IOPRIO_CLASS_IDLE       (3)
#endif

#if !defined(IOPRIO_WHO_PROCESS)
#define IOPRIO_WHO_PROCESS      (1)
#endif
#if !defined(IOPRIO_WHO_PGRP)
#define IOPRIO_WHO_PGRP         (2)
#endif
#if !defined(IOPRIO_WHO_USER)
#define IOPRIO_WHO_USER         (3)
#endif

#if !defined(IOPRIO_PRIO_VALUE)
#define IOPRIO_PRIO_VALUE(class, data)  (((class) << 13) | (data))
#endif

int32_t stress_get_opt_ionice_class(const char *const str);
void stress_set_iopriority(const int32_t class, const int32_t level);

#endif
