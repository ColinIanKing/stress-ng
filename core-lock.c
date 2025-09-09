/*
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-arch.h"
#include "core-asm-arm.h"
#include "core-asm-ppc64.h"
#include "core-asm-loong64.h"
#include "core-asm-riscv.h"
#include "core-asm-x86.h"
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-pthread.h"
#include "core-lock.h"
#include "core-mmap.h"

#if defined(HAVE_LINUX_FUTEX_H)
#include <linux/futex.h>
#endif

#if defined(HAVE_SEMAPHORE_H)
#include <semaphore.h>
#endif

#if defined(HAVE_SEM_SYSV)
#include <sys/sem.h>
#endif

#if defined(HAVE_THREADS_H)
#include <threads.h>
#endif

#define STRESS_LOCK_MAGIC	(0x387cb9e5)	/* magic when lock is used */
#define STRESS_LOCK_MAGIC_FREE	(0x00000000)	/* magic when lock is free */
#define STRESS_LOCK_MAX		(STRESS_PROCS_MAX * 2)	/* max for 2 per instance */

#define STRESS_LOCK_MAX_BACKOFF	(1U << 18)

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

#if defined(HAVE_THREADS_H) &&			\
    defined(HAVE_MTX_T) &&			\
    defined(HAVE_MTX_DESTROY) &&		\
    defined(HAVE_MTX_INIT)
#define LOCK_METHOD_OSI_C_MTX		(0x0004)
#else
#define LOCK_METHOD_OSI_C_MTX		(0)
#endif

#if defined(HAVE_LINUX_FUTEX_H) &&	\
    defined(__NR_futex) &&		\
    defined(FUTEX_LOCK_PI) &&		\
    defined(FUTEX_UNLOCK_PI) &&		\
    defined(HAVE_SYSCALL)
#define LOCK_METHOD_FUTEX		(0x0008)
#else
#define LOCK_METHOD_FUTEX		(0)
#endif

#if defined(HAVE_ATOMIC_TEST_AND_SET) &&	\
    !defined(STRESS_ARCH_ARM)
#define LOCK_METHOD_ATOMIC_SPINLOCK	(0x0010)
#else
#define LOCK_METHOD_ATOMIC_SPINLOCK	(0)
#endif

#if defined(HAVE_SEMAPHORE_H) && \
    defined(HAVE_LIB_PTHREAD) && \
    defined(HAVE_SEM_POSIX)
#define LOCK_METHOD_SEM_POSIX		(0x0020)
#else
#define LOCK_METHOD_SEM_POSIX		(0)
#endif

#if defined(HAVE_SEM_SYSV) && 	\
    defined(HAVE_KEY_T)
#define LOCK_METHOD_SEM_SYSV		(0x0040)
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

typedef union {
#if LOCK_METHOD_ATOMIC_SPINLOCK != 0
	bool	flag;			/* atomic spinlock flag */
#elif LOCK_METHOD_PTHREAD_SPINLOCK != 0
	pthread_spinlock_t pthread_spinlock;	/* spinlock */
#elif LOCK_METHOD_PTHREAD_MUTEX != 0
	pthread_mutex_t pthread_mutex;	/* mutex */
#elif LOCK_METHOD_OSI_C_MTX != 0
	mtx_t mtx;			/* ISO C mutex */
#elif LOCK_METHOD_FUTEX != 0
	int	futex;			/* futex */
#elif LOCK_METHOD_SEM_POSIX != 0
	sem_t	sem_posix;		/* POSIX semaphore */
#elif LOCK_METHOD_SEM_SYSV != 0
	int 	sem_id;			/* SYS V semaphore */
#endif
} stress_lock_u_t;

typedef struct stress_lock {
	uint32_t	magic;		/* Lock magic struct pattern, zero when not in use */
	stress_lock_u_t u;		/* Lock union */
} stress_lock_t;

typedef struct stress_lock_funcs {
	const char *type;
	int (*init)(struct stress_lock *lock);
	int (*deinit)(struct stress_lock *lock);
	int (*acquire)(struct stress_lock *lock);
	int (*acquire_relax)(struct stress_lock *lock);
	int (*release)(struct stress_lock *lock);
} stress_lock_funcs_t;

static stress_lock_t *stress_locks;
static stress_lock_t *stress_lock_big_lock;

static stress_lock_t *stress_lock_get(void);
static int stress_lock_put(stress_lock_t *lock);

/*
 *  stress_lock_valid()
 *	return true of lock magic is valid
 */
static inline ALWAYS_INLINE bool stress_lock_valid(const stress_lock_t *lock)
{
	return LIKELY(lock && (lock->magic == STRESS_LOCK_MAGIC));
}

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

static int CONST stress_atomic_lock_deinit(stress_lock_t *lock)
{
	(void)lock;

	return 0;
}

static int stress_atomic_lock_acquire(stress_lock_t *lock)
{
	if (LIKELY(lock != NULL)) {
		double t = stress_time_now();

		while (test_and_set(&lock->u.flag) == true) {
			if (UNLIKELY(((stress_time_now() - t) > 5.0) && !stress_continue_flag())) {
				errno = EAGAIN;
				return -1;
			}
		}
		return 0;
	}
	errno = EINVAL;
	return -1;
}

#if defined(HAVE_ASM_X86_PAUSE) ||	\
    defined(HAVE_ASM_LOONG64_DBAR) ||	\
    defined(STRESS_ARCH_PPC64) ||	\
    defined(STRESS_ARCH_PPC) ||	\
    defined(STRESS_ARCH_RISCV)
#define STRESS_LOCK_BACKOFF
#endif

static int stress_atomic_lock_acquire_relax(stress_lock_t *lock)
{
	if (LIKELY(lock != NULL)) {
		double t = stress_time_now();
#if defined(STRESS_LOCK_BACKOFF)
		uint32_t backoff = 1;
#endif

		while (test_and_set(&lock->u.flag) == true) {
#if defined(STRESS_LOCK_BACKOFF)
			register uint32_t i;

			for (i = 0; i < backoff; i++) {
#if defined(HAVE_ASM_X86_PAUSE)
				stress_asm_x86_pause();
#elif defined(HAVE_ASM_LOONG64_DBAR)
				stress_asm_loong64_dbar();
#elif defined(STRESS_ARCH_PPC64)
				stress_asm_ppc64_yield();
#elif defined(STRESS_ARCH_PPC)
				stress_asm_ppc_yield();
#elif defined(STRESS_ARCH_RISCV)
				stress_asm_riscv_pause();
#endif
			}
			/*
			 *  multiple fast cpu pauses on a failed lock acquire
			 *  benefit from exponential backoff
			 */
			backoff = backoff << 1;
			if (backoff > STRESS_LOCK_MAX_BACKOFF)
				backoff = STRESS_LOCK_MAX_BACKOFF;
#else
			(void)shim_sched_yield();
#endif
			if (UNLIKELY(((stress_time_now() - t) > 5.0) && !stress_continue_flag())) {
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

static const stress_lock_funcs_t stress_lock_funcs = {
	"atomic",
	stress_atomic_lock_init,
	stress_atomic_lock_deinit,
	stress_atomic_lock_acquire,
	stress_atomic_lock_acquire_relax,
	stress_atomic_lock_release
};

/*
 *  Locking via pthread spinlock
 */
#elif LOCK_METHOD_PTHREAD_SPINLOCK != 0
static int stress_pthread_spinlock_init(stress_lock_t *lock)
{
	int ret;

	ret = pthread_spin_init(&lock->u.pthread_spinlock, PTHREAD_PROCESS_SHARED);
	if (LIKELY(ret == 0))
		return 0;

	errno = ret;
	return -1;
}

static int stress_pthread_spinlock_deinit(stress_lock_t *lock)
{
	int ret;

	ret = pthread_spin_destroy(&lock->u.pthread_spinlock);
	if (LIKELY(ret == 0))
		return 0;
	errno = ret;
	return -1;
}

static int stress_pthread_spinlock_acquire(stress_lock_t *lock)
{
	int ret;

	ret = pthread_spin_lock(&lock->u.pthread_spinlock);
	if (LIKELY(ret == 0))
		return 0;

	errno = ret;
	return -1;
}

static int stress_pthread_spinlock_release(stress_lock_t *lock)
{
	int ret;

	ret = pthread_spin_unlock(&lock->u.pthread_spinlock);
	if (LIKELY(ret == 0))
		return 0;

	errno = ret;
	return -1;
}

static const stress_lock_funcs_t stress_lock_funcs = {
	"spinlock",
	stress_pthread_spinlock_init,
	stress_pthread_spinlock_deinit,
	stress_pthread_spinlock_acquire,
	stress_pthread_spinlock_acquire,
	stress_pthread_spinlock_release
};

/*
 *  Locking via pthread mutex
 */
#elif LOCK_METHOD_PTHREAD_MUTEX != 0
static int stress_pthread_mutex_init(stress_lock_t *lock)
{
	int ret;

	ret = pthread_mutex_init(&lock->u.pthread_mutex, NULL);
	if (LIKELY(ret == 0))
		return 0;

	errno = ret;
	return -1;
}

static int CONST stress_pthread_mutex_deinit(stress_lock_t *lock)
{
	(void)lock;

	return 0;
}

static int stress_pthread_mutex_acquire(stress_lock_t *lock)
{
	int ret;

	ret = pthread_mutex_lock(&lock->u.pthread_mutex);
	if (LIKELY(ret == 0))
		return 0;

	errno = ret;
	return -1;
}

static int stress_pthread_mutex_release(stress_lock_t *lock)
{
	int ret;

	ret = pthread_mutex_unlock(&lock->u.pthread_mutex);
	if (LIKELY(ret == 0))
		return 0;

	errno = ret;
	return -1;
}

static const stress_lock_funcs_t stress_lock_funcs = {
	"pthread-mutex",
	stress_pthread_mutex_init,
	stress_pthread_mutex_deinit,
	stress_pthread_mutex_acquire,
	stress_pthread_mutex_acquire,
	stress_pthread_mutex_release
};

/*
 *  Locking via OSI C mtx Mutex
 */
#elif LOCK_METHOD_OSI_C_MTX != 0
static int stress_mtx_init(stress_lock_t *lock)
{
	if (LIKELY(mtx_init(&lock->u.mtx, mtx_plain) == thrd_success))
		return 0;

	errno = -ENOSYS;
	return -1;
}

static int stress_mtx_deinit(stress_lock_t *lock)
{
	mtx_destroy(&lock->u.mtx);

	return 0;
}

static int stress_mtx_acquire(stress_lock_t *lock)
{
	if (LIKELY(mtx_lock(&lock->u.mtx) == thrd_success))
		return 0;

	errno = -ENOSYS;
	return -1;
}

static int stress_mtx_release(stress_lock_t *lock)
{
	if (LIKELY(mtx_unlock(&lock->u.mtx) == thrd_success))
		return 0;

	errno = -ENOSYS;
	return -1;
}

static const stress_lock_funcs_t stress_lock_funcs = {
	"OSI-C-mtx",
	stress_mtx_init,
	stress_mtx_deinit,
	stress_mtx_acquire,
	stress_mtx_acquire,
	stress_mtx_release
};

/*
 *  Locking via Linux futex system call API
 */
#elif LOCK_METHOD_FUTEX != 0
static int stress_futex_init(stress_lock_t *lock)
{
	lock->u.futex = 0;

	return 0;
}

static int CONST stress_futex_deinit(stress_lock_t *lock)
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

static const stress_lock_funcs_t stress_lock_funcs = {
	"futex",
	stress_futex_init,
	stress_futex_deinit,
	stress_futex_acquire,
	stress_futex_acquire,
	stress_futex_release
};

/*
 *  Locking via POSIX semaphore
 */
#elif LOCK_METHOD_SEM_POSIX != 0
static int stress_sem_posix_init(stress_lock_t *lock)
{
	return sem_init(&lock->u.sem_posix, 0, 1);
}

static int CONST stress_sem_posix_deinit(stress_lock_t *lock)
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

static const stress_lock_funcs_t stress_lock_funcs = {
	"sem-posix",
	stress_sem_posix_init,
	stress_sem_posix_deinit,
	stress_sem_posix_acquire,
	stress_sem_posix_acquire,
	stress_sem_posix_release
};

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

		if (LIKELY(sem_id >= 0)) {
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

static const stress_lock_funcs_t stress_lock_funcs = {
	"sem-sysv",
	stress_sem_sysv_init,
	stress_sem_sysv_deinit,
	stress_sem_sysv_acquire,
	stress_sem_sysv_acquire,
	stress_sem_sysv_release
};

#else

static int CONST stress_no_lock_fail(stress_lock_t *lock)
{
	(void)lock;

	return -1;
}

static const stress_lock_funcs_t stress_lock_funcs = {
	"no-lock",
	stress_no_lock_fail,
	stress_no_lock_fail,
	stress_no_lock_fail,
	stress_no_lock_fail,
	stress_no_lock_fail
};

#endif

/*
 *  stress_lock_create()
 *	generic lock creation and initialization
 */
void *stress_lock_create(const char *name)
{
	stress_lock_t *lock;

	(void)name;

	if (UNLIKELY(LOCK_METHOD_ALL == (0))) {
		/* Critical, we need to be able to lock somehow! */
		pr_err("core-lock: no locking primitives available\n");
		return NULL;
	}

	lock = stress_lock_get();
	if (UNLIKELY(!lock))
		return NULL;

	if (LIKELY(stress_lock_funcs.init(lock) == 0))
		return lock;

	VOID_RET(int, stress_lock_destroy(lock));
	return NULL;
}

/*
 *  stress_lock_destroy()
 *	generic lock destruction
 */
int stress_lock_destroy(void *lock_handle)
{
	stress_lock_t *lock = (stress_lock_t *)lock_handle;

	if (LIKELY(stress_lock_valid(lock))) {
		(void)stress_lock_funcs.deinit(lock);
		return stress_lock_put(lock);
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

	if (LIKELY(stress_lock_valid(lock)))
		return stress_lock_funcs.acquire(lock);

	errno = EINVAL;
	return -1;
}

/*
 *  stress_lock_acquire_relax()
 *	generic lock acquire (lock) with relaxed backoff
 */
int stress_lock_acquire_relax(void *lock_handle)
{
	stress_lock_t *lock = (stress_lock_t *)lock_handle;

	if (LIKELY(stress_lock_valid(lock)))
		return stress_lock_funcs.acquire_relax(lock);

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

	if (LIKELY(stress_lock_valid(lock)))
		return stress_lock_funcs.release(lock);

	errno = EINVAL;
	return -1;
}

/*
 *  stress_lock_get()
 *	get next free lock from shared locks mapping
 */
static stress_lock_t *stress_lock_get(void)
{
	register size_t i;
	stress_lock_t *lock = NULL;

	if (UNLIKELY(!stress_lock_big_lock))
		return NULL;
	if (UNLIKELY(!stress_lock_valid(stress_lock_big_lock)))
		return NULL;
	if (UNLIKELY(stress_lock_funcs.acquire(stress_lock_big_lock) < 0))
		return NULL;
	for (i = 0; i < STRESS_LOCK_MAX; i++) {
		if (stress_locks[i].magic == STRESS_LOCK_MAGIC_FREE) {
			lock = &stress_locks[i];
			lock->magic = STRESS_LOCK_MAGIC;
			break;
		}
	}
	stress_lock_funcs.release(stress_lock_big_lock);

	return lock;

}

/*
 *  stress_lock_put()
 *	mark a lock as new free to be reused
 */
static int stress_lock_put(stress_lock_t *lock)
{
	if (UNLIKELY(!lock))
		return -1;
	if (UNLIKELY(!stress_lock_valid(lock)))
		return -1;
	if (UNLIKELY(!stress_lock_big_lock))
		return -1;
	if (UNLIKELY(!stress_lock_valid(stress_lock_big_lock)))
		return -1;
	if (UNLIKELY(stress_lock_funcs.acquire(stress_lock_big_lock) < 0))
		return -1;

	(void)shim_memset(lock, 0, sizeof(*lock));

	stress_lock_funcs.release(stress_lock_big_lock);

	return 0;
}

/*
 *  stress_lock_mem_map()
 *	mmap 1 page of shared locks
 */
int stress_lock_mem_map(void)
{
	size_t mmap_size;
	char name[64];

	mmap_size = STRESS_LOCK_MAX * sizeof(*stress_locks);
	stress_locks = (stress_lock_t *)stress_mmap_anon_shared(mmap_size, PROT_READ | PROT_WRITE);
	if (UNLIKELY(stress_locks == MAP_FAILED))
		return -1;

	(void)snprintf(name, sizeof(name), "lock-%s", stress_lock_funcs.type);
	stress_set_vma_anon_name(stress_locks, mmap_size, name);

	stress_lock_big_lock = &stress_locks[0];
	stress_lock_funcs.init(stress_lock_big_lock);
	stress_lock_big_lock->magic = STRESS_LOCK_MAGIC;

	return 0;
}

/*
 *  stress_lock_mem_unmap()
 *	unmap shared locks
 */
void stress_lock_mem_unmap(void)
{
	const size_t mmap_size = STRESS_LOCK_MAX * sizeof(*stress_locks);

	(void)stress_munmap_anon_shared((void *)stress_locks, mmap_size);
	stress_locks = NULL;
	stress_lock_big_lock = NULL;
}

