/*
 * Copyright (C) 2024-2025 Colin Ian King
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
#ifndef CORE_VMSTAT_H
#define CORE_VMSTAT_H

#include "core-attribute.h"

extern WARN_UNUSED int stress_set_status(const char *const opt);
extern WARN_UNUSED int stress_set_vmstat(const char *const opt);
extern WARN_UNUSED int stress_set_thermalstat(const char *const opt);
extern WARN_UNUSED int stress_set_iostat(const char *const opt);
extern WARN_UNUSED int stress_set_raplstat(const char *const opt);
extern WARN_UNUSED char *stress_find_mount_dev(const char *name);
extern void stress_set_vmstat_units(const char *const opt);
extern void stress_vmstat_start(void);
extern void stress_vmstat_stop(void);

#endif
