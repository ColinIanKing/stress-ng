/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_LOCK_H
#define CORE_LOCK_H

extern void *stress_lock_create(void);
extern int stress_lock_destroy(void *lock_handle);
extern int stress_lock_acquire(void *lock_handle);
extern int stress_lock_release(void *lock_handle);

#endif
