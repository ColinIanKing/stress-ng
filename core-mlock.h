/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_MLOCK_H
#define CORE_MLOCK_H

extern int stress_mlock_region(const void *addr_start, const void *addr_end);

#endif
