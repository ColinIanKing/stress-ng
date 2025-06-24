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
#ifndef CORE_PTHREAD_H
#define CORE_PTHREAD_H

#if defined(HAVE_PTHREAD_H)
#include <pthread.h>
#endif

/* pthread wrapped stress_args_t */
typedef struct {
	stress_args_t *args;	/* Stress test args */
	void *data;		/* Per thread private data */
	int pthread_ret;	/* Per thread return value */
} stress_pthread_args_t;

/* pthread porting shims, spinlock or fallback to mutex */
#if defined(HAVE_LIB_PTHREAD)
#if defined(HAVE_LIB_PTHREAD_SPINLOCK) &&	\
    !defined(__DragonFly__) &&			\
    !defined(__OpenBSD__)
typedef pthread_spinlock_t 	shim_pthread_spinlock_t;

#define SHIM_PTHREAD_PROCESS_SHARED		PTHREAD_PROCESS_SHARED
#define SHIM_PTHREAD_PROCESS_PRIVATE		PTHREAD_PROCESS_PRIVATE

#define shim_pthread_spin_lock(lock)		pthread_spin_lock(lock)
#define shim_pthread_spin_unlock(lock)		pthread_spin_unlock(lock)
#define shim_pthread_spin_init(lock, shared)	pthread_spin_init(lock, shared)
#define shim_pthread_spin_destroy(lock)		pthread_spin_destroy(lock)
#else
typedef pthread_mutex_t		shim_pthread_spinlock_t;

#define SHIM_PTHREAD_PROCESS_SHARED		NULL
#define SHIM_PTHREAD_PROCESS_PRIVATE		NULL

#define shim_pthread_spin_lock(lock)		pthread_mutex_lock(lock)
#define shim_pthread_spin_unlock(lock)		pthread_mutex_unlock(lock)
#define shim_pthread_spin_init(lock, shared)	pthread_mutex_init(lock, shared)
#define shim_pthread_spin_destroy(lock)		pthread_mutex_destroy(lock)
#endif
#endif

#endif
