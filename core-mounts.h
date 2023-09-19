/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_MOUNTS_H
#define CORE_MOUNTS_H

extern void stress_mount_free(char *mnts[], const int n);
extern int stress_mount_get(char *mnts[], const int max);

#endif
