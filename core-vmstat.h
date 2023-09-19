/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King
 *
 */
#ifndef CORE_VMSTAT_H
#define CORE_VMSTAT_H

#include "stress-ng.h"

extern WARN_UNUSED int stress_set_status(const char *const opt);
extern WARN_UNUSED int stress_set_vmstat(const char *const opt);
extern WARN_UNUSED int stress_set_thermalstat(const char *const opt);
extern WARN_UNUSED int stress_set_iostat(const char *const opt);
extern WARN_UNUSED char *stress_find_mount_dev(const char *name);
extern void stress_vmstat_start(void);
extern void stress_vmstat_stop(void);

#endif
