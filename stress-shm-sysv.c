/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-madvise.h"
#include "core-mincore.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"

#if defined(HAVE_LINUX_MEMPOLICY_H)
#include <linux/mempolicy.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_SYS_IPC_H)
#include <sys/ipc.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_SYS_SHM_H)
#include <sys/shm.h>
#else
UNEXPECTED
#endif

#if !defined(SHM_HUGE_SHIFT)
#define SHM_HUGE_SHIFT	(26)
#endif
#if !defined(SHM_HUGE_2MB)
#define SHM_HUGE_2MB	(21 << SHM_HUGE_SHIFT)
#endif
#if !defined(SHM_HUGE_1GB)
#define SHM_HUGE_1GB	(30 << SHM_HUGE_SHIFT)
#endif

#define MIN_SHM_SYSV_BYTES	(1 * MB)
#define MAX_SHM_SYSV_BYTES	(256 * MB)
#define DEFAULT_SHM_SYSV_BYTES	(8 * MB)

#define MIN_SHM_SYSV_SEGMENTS	(1)
#define MAX_SHM_SYSV_SEGMENTS	(128)
#define DEFAULT_SHM_SYSV_SEGMENTS (8)

#define MAX_SHM_KEYS		(1U << 16)

static const stress_help_t help[] = {
	{ NULL,	"shm-sysv N",		"start N workers that exercise System V shared memory" },
	{ NULL,	"shm-sysv-bytes N",	"allocate and free N bytes of shared memory per loop" },
	{ NULL,	"shm-sysv-mlock",	"attempt to mlock pages into memory" },
	{ NULL,	"shm-sysv-ops N",	"stop after N shared memory bogo operations" },
	{ NULL,	"shm-sysv-segs N",	"allocate N shared memory segments per iteration" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_SHM_H) &&	\
    defined(HAVE_SHM_SYSV)

#define KEY_GET_RETRIES		(40)
#define BITS_PER_BYTE		(8)
#define NUMA_LONG_BITS		(sizeof(unsigned long int) * BITS_PER_BYTE)
#if !defined(MPOL_F_ADDR)
#define MPOL_F_ADDR		(1 << 1)
#endif
#if !defined(MPOL_DEFAULT)
#define MPOL_DEFAULT		(0)
#endif

/*
 *  Note, running this test with the --maximize option on
 *  low memory systems with many instances can trigger the
 *  OOM killer fairly easily.  The test tries hard to reap
 *  reap shared memory segments that are left over if the
 *  child is killed, however if the OOM killer kills the
 *  parent that does the reaping, then one can be left with
 *  a system with many shared segments still reserved and
 *  little free memory.
 */
typedef struct {
	int	index;
	int	shm_id;
} stress_shm_msg_t;

static const int shm_flags[] = {
#if defined(SHM_HUGETLB) &&	\
    defined(SHM_HUGE_1GB)
	SHM_HUGETLB | SHM_HUGE_1GB,
#endif
#if defined(SHM_HUGETLB) &&	\
    defined(SHM_HUGE_2MB)
	SHM_HUGETLB | SHM_HUGE_2MB,
#endif
#if defined(SHM_HUGETLB)
	SHM_HUGETLB,
#endif
#if defined(SHM_HUGE_2MB)
	SHM_HUGE_2MB,
#endif
#if defined(SHM_HUGE_1GB)
	SHM_HUGE_1GB,
#endif
/* This will segv if no backing, so don't use it for now */
/*
#if defined(SHM_NO_RESERVE)
	SHM_NO_RESERVE
#endif
*/
	0
};
#endif

static const stress_opt_t opts[] = {
	{ OPT_shm_sysv_bytes, "shm-sysv-bytes", TYPE_ID_SIZE_T_BYTES_VM, MIN_SHM_SYSV_BYTES, MAX_MEM_LIMIT, NULL },
	{ OPT_shm_sysv_mlock, "shm-sysv-mlock", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_shm_sysv_segs,  "shm-sysv-segs",  TYPE_ID_SIZE_T, MIN_SHM_SYSV_SEGMENTS, MAX_SHM_SYSV_SEGMENTS, NULL },
	END_OPT,
};

#if defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_SHM_H) &&	\
    defined(HAVE_SHM_SYSV)

static void stress_shm_metrics(
	stress_args_t *args,
	const double duration,
	const double count,
	const char *syscall_name,
	const int idx)
{
	char buffer[40];
	const double rate = (count > 0.0) ? (duration / count) : 0.0;

	(void)snprintf(buffer, sizeof(buffer), "nanosecs per %s call", syscall_name);
	stress_metrics_set(args, idx, buffer,
		STRESS_DBL_NANOSECOND * rate, STRESS_METRIC_HARMONIC_MEAN);
}

/*
 *  stress_shm_sysv_check()
 *	simple check if shared memory is sane
 */
static int OPTIMIZE3 stress_shm_sysv_check(
	uint8_t *buf,
	const size_t sz,
	const size_t page_size)
{
	uint8_t *ptr, val;
	const uint8_t *end = buf + sz;

PRAGMA_UNROLL_N(4)
	for (val = 0, ptr = buf; ptr < end; ptr += page_size, val++) {
		*ptr = val;
	}

PRAGMA_UNROLL_N(4)
	for (val = 0, ptr = buf; ptr < end; ptr += page_size, val++) {
		if (UNLIKELY(*ptr != val))
			return -1;

	}
	return 0;
}

/*
 *  exercise_shmat()
 *	exercise shmat syscall with all possible values of arguments
 */
static void exercise_shmat(
	const int shm_id,
	const size_t sz,
	uint64_t *buffer)
{
	void *addr;
	/* Unaligned buffer */
	const uint8_t *unaligned = ((uint8_t *)buffer) + 1;

	/* Exercise shmat syscall on invalid shm_id */
	addr = shmat(-1, NULL, 0);
	if (UNLIKELY(addr != (void *)-1))
		(void)shmdt(addr);

	/* Exercise shmat syscall on invalid flags */
	addr = shmat(shm_id, NULL, ~0);
	if (UNLIKELY(addr != (void *)-1))
		(void)shmdt(addr);

	/* Exercise valid shmat with all possible values of flags */
#if defined(SHM_RDONLY)
	addr = shmat(shm_id, NULL, SHM_RDONLY);
	if (addr != (void *)-1)
		(void)shmdt(addr);
#else
	UNEXPECTED
#endif

#if defined(SHM_EXEC)
	addr = shmat(shm_id, NULL, SHM_EXEC);
	if (addr != (void *)-1)
		(void)shmdt(addr);
#else
	UNEXPECTED
#endif

#if defined(SHM_RND)
	addr = shmat(shm_id, NULL, SHM_RND);
	if (addr != (void *)-1) {
		(void)shmdt(addr);

#if defined(SHM_REMAP)
		/*  Exercise valid remap */
		addr = shmat(shm_id, addr, SHM_REMAP);
		if (addr != (void *)-1) {
			/* Exercise remap onto itself */
			const void *remap = shmat(shm_id, addr, SHM_REMAP | SHM_RDONLY);

			if (remap != (void *)-1)
				(void)shmdt(remap);
			if (addr != remap)
				(void)shmdt(addr);
		}
#else
	UNEXPECTED
#endif
	}
#else
	UNEXPECTED
#endif

#if defined(SHM_RND)
	addr = mmap(NULL, sz, PROT_READ,
		    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (LIKELY(addr != MAP_FAILED)) {
		(void)munmap(addr, sz);
		addr = shmat(shm_id, addr, SHM_RND);
		if (addr != (void *)-1)
			(void)shmdt(addr);
	}
#else
	UNEXPECTED
#endif

	/* Exercise shmat with SHM_REMAP flag on NULL address */
#if defined(SHM_REMAP)
	addr = shmat(shm_id, NULL, SHM_REMAP);
	if (addr != (void *)-1)
		(void)shmdt(addr);
#else
	UNEXPECTED
#endif

	/* Exercise invalid shmat with unaligned page address */
	addr = shmat(shm_id, unaligned, 0);
	if (addr != (void *)-1)
		(void)shmdt(addr);

	/* Exercise invalid shmdt with unaligned page address */
	VOID_RET(int, shmdt(unaligned));

	/*
	 * Exercise valid shmat syscall with unaligned
	 * page address but specifying SHM_RND flag
	 */
#if defined(SHM_RND) &&	\
    !defined(__FreeBSD__)
	addr = shmat(shm_id, unaligned, SHM_RND);
	if (addr != (void *) -1)
		(void)shmdt(addr);
#else
	UNEXPECTED
#endif
}

#if !defined(STRESS_ARCH_M68K)
/*
 *  get_bad_shmid()
 *	find invalid shared memory segment id
 */
static int get_bad_shmid(stress_args_t *args)
{
	int id = ~0;

	while (stress_continue(args)) {
		int ret;
		struct shmid_ds ds;

		ret = shmctl(id, IPC_STAT, &ds);
		if ((ret < 0) && ((errno == EINVAL) || (errno == EIDRM)))
			return id;

		/* Try again with a random guess */
		id = (int)stress_mwc32();
	}

	return -1;
}
#endif

/*
 *  exercise_shmctl()
 *	exercise shmctl syscall with all possible values of arguments
 */
static void exercise_shmctl(const key_t key, const size_t sz, stress_args_t *args)
{
	int shm_id;
#if !defined(STRESS_ARCH_M68K)
	const int bad_shmid = get_bad_shmid(args);
#endif

	shm_id = shmget(key, sz, IPC_CREAT);
	if (shm_id < 0)
		return;

	/* Exercise invalid commands */
	VOID_RET(int, shmctl(shm_id, -1, NULL));
	VOID_RET(int, shmctl(shm_id, 0x7ffffff, NULL));

#if !defined(STRESS_ARCH_M68K)
	VOID_RET(int, shmctl(shm_id, IPC_SET | IPC_RMID, NULL));

	/* Exercise invalid shmid */
	VOID_RET(int, shmctl(bad_shmid, IPC_RMID, NULL));

	/* Cleaning up the shared memory segment */
	(void)shmctl(shm_id, IPC_RMID, NULL);
#endif

	/* Check for EIDRM error */
#if defined(IPC_STAT) &&	\
    defined(HAVE_SHMID_DS)
	{
		struct shmid_ds buf;
		int ret;

		errno = 0;
		ret = shmctl(shm_id, IPC_STAT, &buf);
		if (UNLIKELY((ret >= 0) && (errno == 0)))
			pr_fail("%s: shmctl IPC_STAT unexpectedly succeeded on non-existent shared "
				"memory segment, errno=%d (%s)\n", args->name, errno, strerror(errno));
	}
#else
	UNEXPECTED
#endif
}

/*
 *  exercise_shmget()
 *	exercise shmget syscall with all possible values of arguments
 */
static void exercise_shmget(const key_t key, const size_t sz, const char *name)
{
	int shm_id;

	/* Exercise invalid flags */
	shm_id = shmget(key, sz, ~0);
	if (shm_id >= 0)
		(void)shmctl(shm_id, IPC_RMID, NULL);

	shm_id = shmget(key, sz, IPC_CREAT);
	if (shm_id >= 0) {
		int shm_id2;
		/*
		 * Exercise invalid shmget by creating an already
		 * existing shared memory segment and IPC_EXCL flag
		 */
		errno = 0;
		shm_id2 = shmget(key, sz, IPC_CREAT | IPC_EXCL);
		if (UNLIKELY((shm_id2 >= 0) && (errno == 0))) {
			pr_fail("%s: shmget IPC_CREAT unexpectedly succeeded and re-created "
				"shared memory segment even with IPC_EXCL flag "
				"specified, errno=%d (%s)\n", name, errno, strerror(errno));
			(void)shmctl(shm_id2, IPC_RMID, NULL);
		}

		/*
		 * Exercise invalid shmget by creating an already
		 * existing shared memory segment but of greater size
		 */
		errno = 0;
		shm_id2 = shmget(key, sz + (1024 * 1024), IPC_CREAT);
		if (UNLIKELY((shm_id2 >= 0) && (errno == 0))) {
			pr_fail("%s: shmget IPC_CREAT unexpectedly succeeded and again "
				"created shared memory segment with a greater "
				"size, errno=%d (%s)\n", name, errno, strerror(errno));
			(void)shmctl(shm_id2, IPC_RMID, NULL);
		}

		(void)shmctl(shm_id, IPC_RMID, NULL);
	}

    /* Exercise shmget on invalid sizes argument*/
#if defined(SHMMIN)
	errno = 0;
	shm_id = shmget(key, SHMMIN - 1, IPC_CREAT);
	if (UNLIKELY((SHMMIN > 0) && (shm_id >= 0))) {
		pr_fail("%s: shmget IPC_CREAT unexpectedly succeeded on invalid value of"
			"size argument, errno=%d (%s)\n", name, errno, strerror(errno));
		(void)shmctl(shm_id, IPC_RMID, NULL);
	}
#else
	/* UNEXPECTED */
#endif

#if defined(SHMMAX)
	errno = 0;
	shm_id = shmget(key, SHMMAX + 1, IPC_CREAT);
	if (UNLIKELY((SHMMAX < ~(size_t)0) && (shm_id >= 0))) {
		pr_fail("%s: shmget IPC_CREAT unexpectedly succeeded on invalid value of"
			"size argument, errno=%d (%s)\n", name, errno, strerror(errno));
		(void)shmctl(shm_id, IPC_RMID, NULL);
	}
#elif defined(__linux__)
	{
		char buf[32];

		/* Find size from shmmax proc value */
		if (stress_system_read("/proc/sys/kernel/shmmax", buf, sizeof(buf)) > 0) {
			size_t shmmax;

			if (sscanf(buf, "%zu", &shmmax) == 1) {
				shmmax++;
				if (shmmax > 0) {
					errno = 0;
					shm_id = shmget(key, shmmax, IPC_CREAT);
					if (UNLIKELY(shm_id >= 0)) {
						pr_fail("%s: shmget IPC_CREAT unexpectedly succeeded on "
							"invalid value %zu of size argument, errno=%d (%s)\n",
							name, shmmax, errno, strerror(errno));
						(void)shmctl(shm_id, IPC_RMID, NULL);
					}
				}
			}
		}
	}
#endif

#if defined(SHM_HUGETLB) &&	\
    defined(SHM_HUGE_2MB)
	shm_id = shmget(IPC_PRIVATE, sz, IPC_CREAT | SHM_HUGETLB | SHM_HUGE_2MB | SHM_R | SHM_W);
	if (shm_id >= 0)
		(void)shmctl(shm_id, IPC_RMID, NULL);
#endif

#if defined(IPC_PRIVATE)
	shm_id = shmget(IPC_PRIVATE, sz, IPC_CREAT);
	if (shm_id >= 0)
		(void)shmctl(shm_id, IPC_RMID, NULL);
#else
	UNEXPECTED
#endif

	errno = 0;
	shm_id = shmget(key, sz, IPC_EXCL);
	if (UNLIKELY((shm_id >= 0) && (errno == 0))) {
		pr_fail("%s: shmget IPC_RMID unexpectedly succeeded on non-existent shared "
			"memory segment, errno=%d (%s)\n", name, errno, strerror(errno));
		(void)shmctl(shm_id, IPC_RMID, NULL);
	}

}

#if defined(__linux__)
/*
 *  stress_shm_get_procinfo()
 *	exercise /proc/sysvipc/shm
 */
static void stress_shm_get_procinfo(bool *get_procinfo)
{
	int fd;
	static int count;

	if (++count & 0x3f)
		return;
	count = 0;

	fd = open("/proc/sysvipc/shm", O_RDONLY);
	if (fd < 0) {
		*get_procinfo = false;
		return;
	}
	for (;;) {
		ssize_t ret;
		char buffer[1024];

		ret = read(fd, buffer, sizeof(buffer));
		if (ret <= 0)
			break;
	}
	(void)close(fd);
}
#endif

#if defined(__linux__)
/*
 *  stress_shm_sysv_linux_proc_map()
 *	exercise the correspronding /proc/$PID/map_files/ mapping
 *	with the shm address space.  Ignore errors, we just want
 *	to exercise the kernel
 */
static void stress_shm_sysv_linux_proc_map(const void *addr, const size_t sz)
{
	int fd;
	char path[PATH_MAX];
	const int len = (int)sizeof(void *);
	const intptr_t start = (intptr_t)addr, end = start + (intptr_t)sz;

	(void)snprintf(path, sizeof(path), "/proc/%d/map_files/%*.*" PRIxPTR "-%*.*" PRIxPTR,
		getpid(), len, len, start, len, len, end);

	/*
	 *  Normally can only open if we have PTRACE_MODE_READ_FSCREDS,
	 *  silently ignore failure
	 */
	fd = open(path, O_RDONLY);
	if (fd >= 0) {
		char pathlink[PATH_MAX];
		void *ptr;

		/*
		 *  Readlink will return the /SYSV key info, but since this kind
		 *  of interface may change format, we skip checking it against
		 *  the key
		 */
		VOID_RET(ssize_t, shim_readlink(path, pathlink, sizeof(pathlink)));

		/*
		 *  The vfs allows us to mmap this file, which corresponds
		 *  to the same physical pages of the shm allocation
		 */
		ptr = mmap(NULL, sz, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS,
			-1, 0);
		if (ptr != MAP_FAILED)
			(void)munmap(ptr, sz);

		/*
		 *  We can fsync it to..
		 */
		(void)fsync(fd);
		(void)close(fd);
	}
}
#endif

/*
 *  stress_shm_sysv_get_key()
 *	find a 'random' unused key
 */
static int stress_shm_sysv_get_key(
	stress_args_t *args,
	const size_t segment,
	const uint32_t keys_per_instance,
	const uint32_t keys_per_segment,
	key_t *key,
	key_t keys[MAX_SHM_SYSV_SEGMENTS])
{
	*key = 0;

	for (;;) {
		key_t new_key;
		size_t i;
		uint32_t offset;

retry:
		if (!stress_continue(args))
			break;

		/* Get a unique random key */
		offset = (args->instance * keys_per_instance) +
		         (segment * keys_per_segment) +
		         stress_mwc32modn(keys_per_segment);
		/* Key should never exceed MAX_SHM_KEYS */
		new_key = offset & (MAX_SHM_KEYS - 1);
		if (!new_key)
			goto retry;

		/* Is it already used in our local cache? */
		for (i = 0; i < MAX_SHM_SYSV_SEGMENTS; i++) {
			if (new_key == keys[i])
				goto retry;
		}
		/* Is it already used in the entire system? */
		if (shmget(new_key, args->page_size, 0) < 0) {
			if (errno == ENOENT) {
				*key = new_key;
				return 0;
			}
		}
	}
	return -1;
}

/*
 *  stress_shm_sysv_child()
 * 	stress out the shm allocations. This can be killed by
 *	the out of memory killer, so we need to keep the parent
 *	informed of the allocated shared memory ids so these can
 *	be reaped cleanly if this process gets prematurely killed.
 */
static int stress_shm_sysv_child(
	stress_args_t *args,
	const int fd,
	const size_t max_sz,
	const size_t page_size,
	const size_t shm_sysv_segments,
	const bool shm_sysv_mlock)
{
	void *addrs[MAX_SHM_SYSV_SEGMENTS];
	key_t keys[MAX_SHM_SYSV_SEGMENTS];
	int shm_ids[MAX_SHM_SYSV_SEGMENTS];
	stress_shm_msg_t msg;
	size_t i;
	int rc = EXIT_SUCCESS;
	bool ok = true;
	int mask = ~0;
	uint32_t instances = args->instances;
	const size_t buffer_size = (page_size / sizeof(uint64_t)) + 1;
	uint64_t *buffer;
	double shmget_duration = 0.0, shmget_count = 0.0;
	double shmat_duration = 0.0, shmat_count = 0.0;
	double shmdt_duration = 0.0, shmdt_count = 0.0;
	uint32_t seg_space = args->instances * shm_sysv_segments;
	uint32_t max_keys = (uint32_t)MAX_SHM_KEYS / seg_space;
	const uint32_t keys_per_instance = MAX_SHM_KEYS / args->instances;
	const uint32_t keys_per_segment = keys_per_instance / shm_sysv_segments;

	max_keys = (max_keys < 1) ? 1 : max_keys;

	if (stress_instance_zero(args))
		pr_dbg("%s: %" PRIu32 " shm-sysv keys per stressor segment (%zu segments)\n",
			args->name, max_keys, shm_sysv_segments);

	buffer = (uint64_t *)calloc(buffer_size, sizeof(*buffer));
	if (!buffer) {
		pr_inf_skip("%s: failed to allocate %zu byte buffer%s, skipping stressor\n",
			args->name, buffer_size, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		free(buffer);
		return EXIT_FAILURE;
	}

	(void)shim_memset(addrs, 0, sizeof(addrs));
	(void)shim_memset(keys, 0, sizeof(keys));
	for (i = 0; i < MAX_SHM_SYSV_SEGMENTS; i++)
		shm_ids[i] = -1;

	/* Make sure this is killable by OOM killer */
	stress_set_oom_adjustment(args, true);

	do {
		size_t sz = max_sz;
		pid_t pid = -1;
		key_t key;

		/* find key without an identifier associated with it */
		if (stress_shm_sysv_get_key(args, i,
					keys_per_instance,
					keys_per_segment,
					&key, keys) == 0) {
			exercise_shmget(key, sz, args->name);
			exercise_shmctl(key, sz, args);
		}

		for (i = 0; i < shm_sysv_segments; i++) {
			int shm_id = -1, count = 0;
			void *addr;
			size_t shmall, freemem, totalmem, freeswap, totalswap;
			double t;

			/* Try hard not to overcommit at this current time */
			stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);
			shmall /= instances;
			freemem /= instances;
			if ((shmall > page_size) && sz > shmall)
				sz = shmall;
			if ((freemem > page_size) && sz > freemem)
				sz = freemem;
			if (UNLIKELY(!stress_continue_flag()))
				goto reap;

			for (count = 0; count < KEY_GET_RETRIES; count++) {
				int rnd, rnd_flag;
retry:
				rnd = stress_mwc32modn(SIZEOF_ARRAY(shm_flags));
				rnd_flag = shm_flags[rnd] & mask;

				if (sz < page_size)
					goto reap;

				/* find key without an identifier associated with it */
				if (stress_shm_sysv_get_key(args, i,
						keys_per_instance,
						keys_per_segment,
						&key, keys) < 0)
					goto reap;

				t = stress_time_now();
errno = 0;
				shm_id = shmget(key, sz,
					IPC_CREAT | IPC_EXCL |
					S_IRUSR | S_IWUSR | rnd_flag);
				if (LIKELY(shm_id >= 0)) {
					shmget_duration += stress_time_now() - t;
					shmget_count += 1.0;
					break;
				}
				if (errno == EINTR)
					goto reap;
				if (errno == EPERM) {
					/* ignore using the flag again */
					mask &= ~rnd_flag;
					goto retry;
				}
				if ((errno == EINVAL) || (errno == ENOMEM)) {
					/*
					 * On some systems we may need
					 * to reduce the size
					 */
					sz = sz / 2;
				}
			}
			if (UNLIKELY(shm_id < 0)) {
				/* Run out of shm segments, just reap and die */
				if (errno == ENOSPC) {
					pr_inf_skip("%s: shmget ran out of free space, "
						"skipping stressor\n", args->name);
					rc = EXIT_NO_RESOURCE;
					ok = false;
					goto reap;
				}
				/* Run out of shm space, or existing key, so reap, die, repawn */
				if ((errno == ENOMEM) || (errno == EEXIST)) {
					rc = EXIT_SUCCESS;
					goto reap;
				}
				/* Some unexpected failures handler here */
				ok = false;
				pr_fail("%s: shmget failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto reap;
			}

			/* Inform parent of the new shm ID */
			msg.index = (int)i;
			msg.shm_id = shm_id;
			if (UNLIKELY(write(fd, &msg, sizeof(msg)) < 0)) {
				if (errno != EINTR) {
					pr_err("%s: write failed, errno=%d: (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
				goto reap;
			}

			t = stress_time_now();
			addr = shmat(shm_id, NULL, 0);
			if (UNLIKELY(addr == (char *)-1)) {
				if ((errno == EINVAL) || (errno == EIDRM))
					goto reap;
				ok = false;
				pr_fail("%s: shmat on NULL address failed on id %d, (key=%d, size=%zd), errno=%d (%s)\n",
					args->name, shm_id, (int)key, sz, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto reap;
			}
			if (shm_sysv_mlock)
				(void)shim_mlock(addr, sz);
			shmat_duration += stress_time_now() - t;
			shmat_count += 1.0;
			addrs[i] = addr;
			shm_ids[i] = shm_id;
			keys[i] = key;

			if (UNLIKELY(!stress_continue(args)))
				goto reap;
			(void)stress_mincore_touch_pages(addr, sz);
			(void)shim_msync(addr, sz, stress_mwc1() ? MS_ASYNC : MS_SYNC);

			if (UNLIKELY(!stress_continue(args)))
				goto reap;
			(void)stress_madvise_randomize(addr, sz);

			if (UNLIKELY(!stress_continue(args)))
				goto reap;
			if (UNLIKELY(stress_shm_sysv_check(addr, sz, page_size) < 0)) {
				ok = false;
				pr_fail("%s: memory check failed\n", args->name);
				rc = EXIT_FAILURE;
				goto reap;
			}
#if defined(SHM_LOCK) &&	\
    defined(SHM_UNLOCK)
			{
				int ret;

				ret = shmctl(shm_id, SHM_LOCK, NULL);
				if (ret == 0) {
					VOID_RET(int, shmctl(shm_id, SHM_UNLOCK, NULL));
				}
			}
#else
			UNEXPECTED
#endif
#if defined(IPC_STAT) &&	\
    defined(HAVE_SHMID_DS)
			{
				struct shmid_ds ds;

				if (UNLIKELY(shmctl(shm_id, IPC_STAT, &ds) < 0))
					pr_fail("%s: shmctl IPC_STAT failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
#if defined(SHM_SET)
				else
					VOID_RET(int, shmctl(shm_id, SHM_SET, &ds));
#else
				/* UNEXPECTED */
#endif
			}
#else
			UNEXPECTED
#endif
#if defined(IPC_INFO) &&	\
    defined(HAVE_SHMINFO)
			{
				struct shminfo s;

				if (UNLIKELY(shmctl(shm_id, IPC_INFO, (struct shmid_ds *)&s) < 0))
					pr_fail("%s: shmctl IPC_INFO failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#else
			UNEXPECTED
#endif
#if defined(SHM_INFO) &&	\
    defined(HAVE_SHMINFO)
			{
				struct shm_info s;

				if (UNLIKELY(shmctl(shm_id, SHM_INFO, (struct shmid_ds *)&s) < 0))
					pr_fail("%s: shmctl SHM_INFO failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#else
			UNEXPECTED
#endif
#if defined(SHM_LOCK) &&	\
    defined(SHM_UNLOCK)
			if (shmctl(shm_id, SHM_LOCK, (struct shmid_ds *)NULL) < 0) {
				VOID_RET(int, shmctl(shm_id, SHM_UNLOCK, (struct shmid_ds *)NULL));
			}
#else
			UNEXPECTED
#endif
			/*
			 *  Exercise NUMA mem_policy on shm
			 */
#if defined(__NR_get_mempolicy) &&	\
    defined(__NR_set_mempolicy)
			{
				int ret, mode;
				unsigned long int node_mask[NUMA_LONG_BITS];

				ret = shim_get_mempolicy(&mode, node_mask, 1,
					addrs[i], MPOL_F_ADDR);
				if (ret == 0) {
					VOID_RET(int, shim_set_mempolicy(MPOL_DEFAULT, NULL, 1));
				}
			}
#endif
#if defined(__linux__)
			stress_shm_sysv_linux_proc_map(addr, sz);
#endif
			exercise_shmat(shm_id, sz, buffer);
			stress_bogo_inc(args);
		}

		pid = fork();
		if (pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_RUN);

			for (i = 0; i < shm_sysv_segments; i++) {
#if defined(IPC_STAT) &&	\
    defined(HAVE_SHMID_DS)

				if (shm_ids[i] >= 0) {
					struct shmid_ds ds;

					VOID_RET(int, shmctl(shm_ids[i], IPC_STAT, &ds));
				}
#else
				UNEXPECTED
#endif
				VOID_RET(int, shmdt(addrs[i]));

			}
			/* Exercise repeated shmdt on addresses, EINVAL */
			for (i = 0; i < shm_sysv_segments; i++) {
				VOID_RET(int, shmdt(addrs[i]));
			}

			_exit(EXIT_SUCCESS);
		}
reap:
		for (i = 0; i < shm_sysv_segments; i++) {
			if (addrs[i]) {
				double t;

				t = stress_time_now();
				if (UNLIKELY(shmdt(addrs[i]) < 0)) {
					pr_fail("%s: shmdt failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				} else {
					shmdt_duration += stress_time_now() - t;
					shmdt_count += 1.0;
				}
			}
			if (shm_ids[i] >= 0) {
				if (shmctl(shm_ids[i], IPC_RMID, NULL) < 0) {
					if (UNLIKELY((errno != EIDRM) && (errno != EINVAL))) {
						pr_fail("%s: shmctl IPC_RMID failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
					}
				}
			}

			/* Inform parent shm ID is now free */
			msg.index = (int)i;
			msg.shm_id = -1;
			if (UNLIKELY(write(fd, &msg, sizeof(msg)) < 0)) {
				if (errno != EINTR) {
					pr_dbg("%s: write failed, errno=%d: (%s)\n",
						args->name, errno, strerror(errno));
					ok = false;
				}
			}
			addrs[i] = NULL;
			shm_ids[i] = -1;
			keys[i] = 0;
		}

		if (pid >= 0) {
			int status;

			(void)waitpid(pid, &status, 0);
		}
	} while (ok && stress_continue(args));

	/* Inform parent of end of run */
	msg.index = -1;
	msg.shm_id = -1;
	if (write(fd, &msg, sizeof(msg)) < 0) {
		if (errno != EINTR) {
			pr_err("%s: write failed, errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
		}
	}
	free(buffer);

	stress_shm_metrics(args, shmget_duration, shmget_count, "shmget", 0);
	stress_shm_metrics(args, shmat_duration, shmat_count, "shmat", 1);
	stress_shm_metrics(args, shmdt_duration, shmdt_count, "shmdt", 2);
	return rc;
}


/*
 *  stress_shm_sysv()
 *	stress SYSTEM V shared memory
 */
static int stress_shm_sysv(stress_args_t *args)
{
	const size_t page_size = args->page_size;
	size_t orig_sz, sz;
	int pipefds[2];
	int rc = EXIT_SUCCESS;
	ssize_t i;
	pid_t pid;
	bool retry = true;
	bool shm_sysv_mlock = false;
	uint32_t restarts = 0;
	size_t shm_sysv_bytes, shm_sysv_bytes_total = DEFAULT_SHM_SYSV_BYTES;
	size_t shm_sysv_segments = DEFAULT_SHM_SYSV_SEGMENTS;

	if (!stress_get_setting("shm-sysv-mlock", &shm_sysv_mlock)) {
		if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
			shm_sysv_mlock = true;
	}

	if (!stress_get_setting("shm-sysv-bytes", &shm_sysv_bytes_total)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			shm_sysv_bytes_total = MAX_SHM_SYSV_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			shm_sysv_bytes_total = MIN_SHM_SYSV_BYTES;
	}
	shm_sysv_bytes = shm_sysv_bytes_total / args->instances;
	if (shm_sysv_bytes < page_size)
		shm_sysv_bytes = page_size;
	if (stress_instance_zero(args))
		stress_usage_bytes(args, shm_sysv_bytes, shm_sysv_bytes * args->instances);

	if (!stress_get_setting("shm-sysv-segs", &shm_sysv_segments)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			shm_sysv_segments = MAX_SHM_SYSV_SEGMENTS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			shm_sysv_segments = MIN_SHM_SYSV_SEGMENTS;
	}
	shm_sysv_segments /= args->instances;
	if (shm_sysv_segments < 1)
		shm_sysv_segments = 1;

	orig_sz = sz = shm_sysv_bytes & ~(page_size - 1);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	while (LIKELY(stress_continue_flag() && retry)) {
		if (pipe(pipefds) < 0) {
			pr_fail("%s: pipe failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
fork_again:
		pid = fork();
		if (pid < 0) {
			/* Can't fork, retry? */
			if (errno == EAGAIN)
				goto fork_again;
			pr_err("%s: fork failed, errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(pipefds[0]);
			(void)close(pipefds[1]);

			/* Nope, give up! */
			return EXIT_FAILURE;
		} else if (pid > 0) {
			/* Parent */
			int status, shm_ids[MAX_SHM_SYSV_SEGMENTS];
#if defined(__linux__)
			bool get_procinfo = true;
#endif

			stress_set_oom_adjustment(args, false);
			(void)close(pipefds[1]);

			for (i = 0; i < (ssize_t)MAX_SHM_SYSV_SEGMENTS; i++)
				shm_ids[i] = -1;

			while (stress_continue_flag()) {
				stress_shm_msg_t msg;
				ssize_t n;

				/*
				 *  Blocking read on child shm ID info
				 *  pipe.  We break out if pipe breaks
				 *  on child death, or child tells us
				 *  about its demise.
				 */
				n = read(pipefds[0], &msg, sizeof(msg));
				if (n <= 0) {
					if ((errno == EAGAIN) || (errno == EINTR))
						continue;
					if (errno) {
						pr_fail("%s: read failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						break;
					}
					pr_fail("%s: zero bytes read\n", args->name);
					break;
				}
				if ((msg.index < 0) ||
				    (msg.index >= MAX_SHM_SYSV_SEGMENTS)) {
					retry = false;
					break;
				}
				shm_ids[msg.index] = msg.shm_id;
#if defined(__linux__)
				if (get_procinfo)
					stress_shm_get_procinfo(&get_procinfo);
#endif
			}
			(void)shim_kill(pid, SIGALRM);
			(void)shim_waitpid(pid, &status, 0);
			if (WIFSIGNALED(status)) {
				if ((WTERMSIG(status) == SIGKILL) ||
				    (WTERMSIG(status) == SIGBUS)) {
					stress_log_system_mem_info();
					pr_dbg("%s: assuming PID %" PRIdMAX " killed by OOM killer, "
						"restarting again (instance %" PRIu32 ")\n",
						args->name, (intmax_t)pid, args->instance);
					restarts++;
				}
			}
			if (WIFEXITED(status))
				rc = WEXITSTATUS(status);
			(void)close(pipefds[0]);
			/*
			 *  The child may have been killed by the OOM killer or
			 *  some other way, so it may have left the shared
			 *  memory segment around.  At this point the child
			 *  has died, so we should be able to remove the
			 *  shared memory segment.
			 */
			for (i = 0; i < (ssize_t)shm_sysv_segments; i++) {
				if (shm_ids[i] != -1)
					(void)shmctl(shm_ids[i], IPC_RMID, NULL);
			}
		} else {
			/* Child, stress memory */
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			/*
			 * Nicing the child may OOM it first as this
			 * doubles the OOM score
			 */
			errno = 0;
			VOID_RET(int, shim_nice(5));
			if (errno != 0)
				pr_dbg("%s: nice of child failed, errno=%d (%s) "
					"(instance %" PRIu32 ")\n", args->name,
					errno, strerror(errno),
					args->instance);

			(void)close(pipefds[0]);
			rc = stress_shm_sysv_child(args, pipefds[1], sz, page_size, shm_sysv_segments, shm_sysv_mlock);
			(void)close(pipefds[1]);
			_exit(rc);
		}
	}
	if (orig_sz != sz)
		pr_dbg("%s: reduced shared memory size from "
			"%zu to %zu bytes\n", args->name, orig_sz, sz);
	if (restarts) {
		pr_dbg("%s: OOM restarts: %" PRIu32 "\n",
			args->name, restarts);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_shm_sysv_info = {
	.stressor = stress_shm_sysv,
	.classifier = CLASS_VM | CLASS_OS | CLASS_IPC,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_shm_sysv_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_OS | CLASS_IPC,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without System V shared memory shmat() shmdt() system calls"
};
#endif
