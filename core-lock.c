/*
 * Copyright (C) 2022-2024 Colin Ian King.
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
#include "core-asm-arm.h"
#include "core-asm-x86.h"
#include "core-builtin.h"
#include "core-pthread.h"
#include "core-lock.h"

#if defined(HAVE_LINUX_FUTEX_H)
#include <linux/futex.h>
#endif

#if defined(HAVE_SEMAPHORE_H)
#include <semaphore.h>
#endif

#if defined(HAVE_SEM_SYSV)
#include <sys/sem.h>
#endif

#define STRESS_LOCK_MAGIC	(0x387cb9e5)

#if defined(HAVE_LIB_PTHREAD) &&		\
    defined(HAVE_LIB_PTHREAD_SPINLOCK) &&       \
    !defined(__DragonFly__) &&                  \
    !defined(__OpenBSD__)
#define LOCK_METHOD_PTHREAD_SPINLOCK	(0x0001)
#else
#define LOCK_METHOD_PTHREAD_SPINLOCK	(0)
#endif

#if defined(HAVE_LIB_PTHREAD) &&		\
    defined(HAVE_PTHREAD_MUTEX_T) &&       	\
    defined(HAVE_PTHREAD_MUTEX_DESTROY) &&	\
    defined(HAVE_PTHREAD_MUTEX_INIT)
#define LOCK_METHOD_PTHREAD_MUTEX	(0x0002)
#else
#define LOCK_METHOD_PTHREAD_MUTEX	(0)
#endif

#if defined(HAVE_LINUX_FUTEX_H) &&	\
    defined(__NR_futex) &&		\
    defined(FUTEX_LOCK_PI) &&		\
    defined(FUTEX_UNLOCK_PI) &&		\
    defined(HAVE_SYSCALL)
#define LOCK_METHOD_FUTEX		(0x0004)
#else
#define LOCK_METHOD_FUTEX		(0)
#endif

#if defined(HAVE_ATOMIC_TEST_AND_SET)
#define LOCK_METHOD_ATOMIC_SPINLOCK	(0x0008)
#else
#define LOCK_METHOD_ATOMIC_SPINLOCK	(0)
#endif

#if defined(HAVE_SEMAPHORE_H) && \
    defined(HAVE_LIB_PTHREAD) && \
    defined(HAVE_SEM_POSIX)
#define LOCK_METHOD_SEM_POSIX		(0x0010)
#else
#define LOCK_METHOD_SEM_POSIX		(0)
#endif

#if defined(HAVE_SEM_SYSV) && 	\
    defined(HAVE_KEY_T)
#define LOCK_METHOD_SEM_SYSV		(0x0020)
#else
#define LOCK_METHOD_SEM_SYSV		(0)
#endif

#define LOCK_METHOD_ALL			\
	(LOCK_METHOD_ATOMIC_SPINLOCK |	\
	 LOCK_METHOD_PTHREAD_SPINLOCK | \
	 LOCK_METHOD_PTHREAD_MUTEX |	\
	 LOCK_METHOD_FUTEX |		\
	 LOCK_METHOD_SEM_POSIX | 	\
	 LOCK_METHOD_SEM_SYSV)

typedef struct stress_lock {
	uint32_t	magic;		/* Lock magic struct pattern */
	int		method;		/* Lock method */
	char 		*type;		/* User readable lock type */
	union {
#if LOCK_METHOD_ATOMIC_SPINLOCK != 0
		bool	flag;		/* atomic spinlock flag */
#endif
#if LOCK_METHOD_PTHREAD_SPINLOCK != 0
		pthread_spinlock_t pthread_spinlock;	/* spinlock */
#endif
#if LOCK_METHOD_PTHREAD_MUTEX != 0
		pthread_mutex_t pthread_mutex;	/* mutex */
#endif
#if LOCK_METHOD_FUTEX != 0
		int	futex;		/* futex */
#endif
#if LOCK_METHOD_SEM_POSIX != 0
		sem_t	sem_posix;	/* POSIX semaphore */
#endif
#if LOCK_METHOD_SEM_SYSV != 0
		int 	sem_id;		/* SYS V semaphore */
#endif
	} u;
	int (*init)(struct stress_lock *lock);
	int (*deinit)(struct stress_lock *lock);
	int (*acquire)(struct stress_lock *lock);
	int (*release)(struct stress_lock *lock);
} stress_lock_t;

/*
 *  Locking via atomic spinlock
 */
#if LOCK_METHOD_ATOMIC_SPINLOCK != 0
static inline bool test_and_set(bool *addr)
{
	return __atomic_test_and_set((void *)addr, __ATOMIC_ACQ_REL);
}

static int stress_atomic_lock_init(stress_lock_t *lock)
{
	lock->u.flag = 0;

	return 0;
}

static int stress_atomic_lock_deinit(stress_lock_t *lock)
{
	(void)lock;

	return 0;
}

static int stress_atomic_lock_acquire(stress_lock_t *lock)
{
	if (lock) {
		double t = stress_time_now();

		while (test_and_set(&lock->u.flag) == true) {
#if defined(HAVE_ASM_X86_PAUSE)
			stress_asm_x86_pause();
#elif defined(HAVE_ASM_ARM_YIELD)
			stress_asm_arm_yield();
#elif defined(HAVE_ASM_LOONG64_DBAR)
			stress_waitcpu_loong64_dbar();
#elif defined(STRESS_ARCH_PPC64)
			stress_asm_ppc64_yield();
#elif defined(STRESS_ARCH_RISCV)
			stress_asm_riscv_pause();
#else
			shim_sched_yield();
#endif
			if (((stress_time_now() - t) > 5.0) && !stress_continue_flag()) {
				errno = EAGAIN;
				return -1;
			}
		}
		return 0;
	}
	errno = EINVAL;
	return -1;
}

static int stress_atomic_lock_release(stress_lock_t *lock)
{
	lock->u.flag = false;

	return 0;
}

/*
 *  Locking via pthread spinlock
 */
#elif LOCK_METHOD_PTHREAD_SPINLOCK != 0
static int stress_pthread_spinlock_init(stress_lock_t *lock)
{
	int ret;

	ret = pthread_spin_init(&lock->u.pthread_spinlock, PTHREAD_PROCESS_SHARED);
	if (ret == 0)
		return 0;

	errno = ret;
	return -1;
}

static int stress_pthread_spinlock_deinit(stress_lock_t *lock)
{
	(void)lock;

	return 0;
}

static int stress_pthread_spinlock_acquire(stress_lock_t *lock)
{
	int ret;

	ret = pthread_spin_lock(&lock->u.pthread_spinlock);
	if (ret == 0)
		return 0;

	errno = ret;
	return -1;
}

static int stress_pthread_spinlock_release(stress_lock_t *lock)
{
	int ret;

	ret = pthread_spin_unlock(&lock->u.pthread_spinlock);
	if (ret == 0)
		return 0;

	errno = ret;
	return -1;
}

/*
 *  Locking via pthread mutex
 */
#elif LOCK_METHOD_PTHREAD_MUTEX != 0
static int stress_pthread_mutex_init(stress_lock_t *lock)
{
	int ret;

	ret = pthread_mutex_init(&lock->u.pthread_mutex, NULL);
	if (ret == 0)
		return 0;

	errno = ret;
	return -1;
}

static int stress_pthread_mutex_deinit(stress_lock_t *lock)
{
	(void)lock;

	return 0;
}

static int stress_pthread_mutex_acquire(stress_lock_t *lock)
{
	int ret;

	ret = pthread_mutex_lock(&lock->u.pthread_mutex);
	if (ret == 0)
		return 0;

	errno = ret;
	return -1;
}

static int stress_pthread_mutex_release(stress_lock_t *lock)
{
	int ret;

	ret = pthread_mutex_unlock(&lock->u.pthread_mutex);
	if (ret == 0)
		return 0;

	errno = ret;
	return -1;
}

/*
 *  Locking via Linux futex system call API
 */
#elif LOCK_METHOD_FUTEX != 0
static int stress_futex_init(stress_lock_t *lock)
{
	lock->u.futex = 0;

	return 0;
}

static int stress_futex_deinit(stress_lock_t *lock)
{
	(void)lock;

	return 0;
}

static int stress_futex_acquire(stress_lock_t *lock)
{
	return (int)syscall(__NR_futex, &lock->u.futex, FUTEX_LOCK_PI, 0, 0, 0, 0);
}

static int stress_futex_release(stress_lock_t *lock)
{
	return (int)syscall(__NR_futex, &lock->u.futex, FUTEX_UNLOCK_PI, 0, 0, 0, 0);
}

/*
 *  Locking via POSIX semaphore
 */
#elif LOCK_METHOD_SEM_POSIX != 0
static int stress_sem_posix_init(stress_lock_t *lock)
{
	return sem_init(&lock->u.sem_posix, 0, 1);
}

static int stress_sem_posix_deinit(stress_lock_t *lock)
{
	(void)lock;

	return 0;
}

static int stress_sem_posix_acquire(stress_lock_t *lock)
{
	return sem_wait(&lock->u.sem_posix);
}

static int stress_sem_posix_release(stress_lock_t *lock)
{
	return sem_post(&lock->u.sem_posix);
}

/*
 *  Locking via SYSV semaphore
 */
#elif LOCK_METHOD_SEM_SYSV != 0
static int stress_sem_sysv_init(stress_lock_t *lock)
{
	int i;

	for (i = 0; i < 256; i++) {
		const key_t key_id = (key_t)stress_mwc16();
		const int sem_id = semget(key_id, 1, IPC_CREAT | S_IRUSR | S_IWUSR);

		if (sem_id >= 0) {
			union semun {
				int val;
			} arg;

			arg.val = 1;
			if (semctl(sem_id, 0, SETVAL, arg) == 0) {
				lock->u.sem_id = sem_id;
				return 0;
			}
			break;
		}
	}
	errno = ENOENT;
	return -1;
}

static int stress_sem_sysv_deinit(stress_lock_t *lock)
{
	return semctl(lock->u.sem_id, 0, IPC_RMID);
}

static int stress_sem_sysv_acquire(stress_lock_t *lock)
{
	struct sembuf sops[1];

	sops[0].sem_num = 0;
	sops[0].sem_op = -1;
	sops[0].sem_flg = SEM_UNDO;

	return semop(lock->u.sem_id, sops, 1);
}

static int stress_sem_sysv_release(stress_lock_t *lock)
{
	struct sembuf sops[1];

	sops[0].sem_num = 0;
	sops[0].sem_op = 1;
	sops[0].sem_flg = SEM_UNDO;

	return semop(lock->u.sem_id, sops, 1);
}
#endif

static bool stress_lock_valid(const stress_lock_t *lock)
{
	return (lock && (lock->magic == STRESS_LOCK_MAGIC));
}

/*
 *  stress_lock_create()
 *	generic lock creation and initialization
 */
void *stress_lock_create(void)
{
	stress_lock_t *lock;

	errno = ENOMEM;
	if (LOCK_METHOD_ALL == (0))
		goto no_locks;

	lock = (stress_lock_t *)stress_mmap_populate(NULL, sizeof(*lock),
		PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (lock == MAP_FAILED)
		return NULL;

	/*
	 *  Select locking implementation, try to use fast atomic
	 *  spinlock, then pthread spinlock, then pthread mutex
	 *  and fall back on Linux futex
	 */
#if LOCK_METHOD_ATOMIC_SPINLOCK != 0
	lock->init = stress_atomic_lock_init;
	lock->deinit = stress_atomic_lock_deinit;
	lock->acquire = stress_atomic_lock_acquire;
	lock->release = stress_atomic_lock_release;
	lock->type = "atomic-spinlock";
#elif LOCK_METHOD_PTHREAD_SPINLOCK != 0
	lock->init = stress_pthread_spinlock_init;
	lock->deinit = stress_pthread_spinlock_deinit;
	lock->acquire = stress_pthread_spinlock_acquire;
	lock->release = stress_pthread_spinlock_release;
	lock->type = "pthread-spinlock";
#elif LOCK_METHOD_PTHREAD_MUTEX != 0
	lock->init = stress_pthread_mutex_init;
	lock->deinit = stress_pthread_mutex_deinit;
	lock->acquire = stress_pthread_mutex_acquire;
	lock->release = stress_pthread_mutex_release;
	lock->type = "pthread-mutex";
#elif LOCK_METHOD_FUTEX != 0
	lock->init = stress_futex_init;
	lock->deinit = stress_futex_deinit;
	lock->acquire = stress_futex_acquire;
	lock->release = stress_futex_release;
	lock->type = "futex";
#elif LOCK_METHOD_SEM_POSIX != 0
	lock->init = stress_sem_posix_init;
	lock->deinit = stress_sem_posix_deinit;
	lock->acquire = stress_sem_posix_acquire;
	lock->release = stress_sem_posix_release;
	lock->type = "sem-posix";
#elif LOCK_METHOD_SEM_SYSV != 0
	lock->init = stress_sem_sysv_init;
	lock->deinit = stress_sem_sysv_deinit;
	lock->acquire = stress_sem_sysv_acquire;
	lock->release = stress_sem_sysv_release;
	lock->type = "sem-posix";
#else
	(void)munmap((void *)lock, sizeof(*lock));
	goto no_locks;
#endif
	lock->magic = STRESS_LOCK_MAGIC;

	if (lock->init(lock) == 0)
		return lock;

	VOID_RET(int, stress_lock_destroy(lock));

	return NULL;

no_locks:
	/* Critical, we need to be able to lock somehow! */
	pr_err("core-lock: no locking primitives available\n");
	return NULL;
}

/*
 *  stress_lock_destroy()
 *	generic lock destruction
 */
int stress_lock_destroy(void *lock_handle)
{
	stress_lock_t *lock = (stress_lock_t *)lock_handle;

	if (stress_lock_valid(lock)) {
		(void)lock->deinit(lock);
		(void)shim_memset(lock, 0, sizeof(*lock));
		(void)munmap((void *)lock, sizeof(*lock));
		return 0;
	}
	errno = EINVAL;
	return -1;
}

/*
 *  stress_lock_acquire()
 *	generic lock acquire (lock)
 */
int stress_lock_acquire(void *lock_handle)
{
	stress_lock_t *lock = (stress_lock_t *)lock_handle;

	if (stress_lock_valid(lock))
		return lock->acquire(lock);

	errno = EINVAL;
	return -1;
}

/*
 *  stress_lock_release()
 *	generic lock release (unlock)
 */
int stress_lock_release(void *lock_handle)
{
	stress_lock_t *lock = (stress_lock_t *)lock_handle;

	if (stress_lock_valid(lock))
		return lock->release(lock);

	errno = EINVAL;
	return -1;
}
