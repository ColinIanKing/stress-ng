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
#include "stress-ng.h"
#include "core-asm-generic.h"
#include "core-asm-x86.h"
#include "core-asm-riscv.h"
#include "core-asm-ppc64.h"
#include "core-affinity.h"
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-numa.h"
#include "core-put.h"

#include <math.h>
#include <sched.h>

#define N_FUNCS (SIZEOF_ARRAY(stress_cachehammer_funcs))

#if defined(STRESS_ARCH_RISCV) &&	\
    defined(HAVE_ASM_RISCV_CBO_ZERO) &&	\
    defined(__NR_riscv_hwprobe) && \
    defined(RISCV_HWPROBE_EXT_ZICBOZ)
#define HAVE_RISCV_CBO_ZERO
#endif

typedef void (*hammer_func_t)(stress_args_t *args, void *addr1, void *addr2,
		       const bool is_bad_addr, const bool verify);

typedef struct {
	char *name;
	bool (*valid)(void);
	hammer_func_t hammer;
} stress_cachehammer_func_t;

static const stress_help_t help[] = {
	{ NULL,	"cachehammer N",	"start N CPU cache thrashing workers" },
	{ NULL,	"cachehammer-numa",	"move pages to randomly chosen NUMA nodes" },
	{ NULL,	"cachehammer-ops N",	"stop after N cache bogo operations" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_cachehammer_numa, "cachehammer-numa",  TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_SIGLONGJMP)

static sigjmp_buf jmp_env;

#if defined(HAVE_MSYNC)
static int msync_flags[] = {
#if defined(MS_SYNC)
	MS_SYNC,
#endif
#if defined(MS_ASYNC)
	MS_ASYNC,
#endif
#if defined(MS_SYNC) && defined(MS_INVALIDATE)
	MS_SYNC | MS_INVALIDATE,
#endif
#if defined(MS_ASYNC) && defined(MS_INVALIDATE)
	MS_ASYNC | MS_INVALIDATE,
#endif
};
#endif

static char cachehammer_filename[PATH_MAX];
static char cachehammer_path[PATH_MAX];

static bool CONST hammer_valid(void)
{
	return true;
}

static void stress_cachehammer_init(const uint32_t instances)
{
	int fd;
	const size_t page_size = stress_get_page_size();
	ssize_t ret;
	uint8_t *page;

	(void)instances;

	(void)memset(cachehammer_filename, 0, sizeof(cachehammer_filename));
	(void)memset(cachehammer_path, 0, sizeof(cachehammer_path));

	page = calloc(page_size, sizeof(*page));
	if (!page)
		goto err_nullstr;
	if (stress_temp_dir(cachehammer_path, sizeof(cachehammer_path),
			    "cachehammer", getpid(), 0) < 0)
		goto err_free;
	if (mkdir(cachehammer_path, S_IRWXU) < 0)
		goto err_rmdir;
	(void)stress_mk_filename(cachehammer_filename,
		sizeof(cachehammer_filename), cachehammer_path, "mmap-page");
	fd = open(cachehammer_filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0)
		goto err_unlink;
	ret = write(fd, page, page_size);
	(void)close(fd);

	if (ret != (ssize_t)page_size)
		goto err_unlink;

	free(page);
	return;

err_unlink:
	(void)unlink(cachehammer_filename);
err_rmdir:
	(void)shim_rmdir(cachehammer_path);
err_free:
	free(page);
err_nullstr:
	*cachehammer_filename = '\0';
	*cachehammer_path = '\0';
	return;
}

static void stress_cachehammer_deinit(void)
{
	if (*cachehammer_filename)
		(void)unlink(cachehammer_filename);
	if (*cachehammer_path)
		(void)shim_rmdir(cachehammer_path);
}

/*
 *  hammer_read()
 *	read 64 bit value from cache/memory
 */
static void OPTIMIZE3 hammer_read(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	volatile const uint64_t *vptr;

	(void)args;
	(void)verify;

	if (UNLIKELY(is_bad_addr))
		return;

	vptr = (volatile uint64_t *)addr1;
	(void)*vptr;
	stress_asm_mb();
	vptr = (volatile uint64_t *)addr2;
	(void)*vptr;
	stress_asm_mb();
}

/*
 *  hammer_read()
 *	read 64 bytes values from cache/memory
 */
static void OPTIMIZE3 hammer_read64(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	volatile uint64_t *vptr1, *vptr2;

	(void)args;
	(void)verify;

	if (UNLIKELY(is_bad_addr))
		return;

	vptr1 = (volatile uint64_t *)addr1;
	vptr2 = (volatile uint64_t *)addr2;

	*(vptr1 + 0);
	stress_asm_mb();
	*(vptr2 + 1);
	stress_asm_mb();
	*(vptr1 + 2);
	stress_asm_mb();
	*(vptr2 + 3);
	stress_asm_mb();
	*(vptr1 + 4);
	stress_asm_mb();
	*(vptr2 + 5);
	stress_asm_mb();
	*(vptr1 + 6);
	stress_asm_mb();
	*(vptr2 + 7);
	stress_asm_mb();
}

/*
 *  hammer_write()
 *	write 64 bit value to cache/memory
 */
static void OPTIMIZE3 hammer_write(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	volatile uint64_t *vptr;
	const uint64_t pattern = (uint64_t)0x55aa5aa5aa55a55aULL;

	if (UNLIKELY(is_bad_addr))
		return;

	vptr = (volatile uint64_t *)addr1;
	*vptr = pattern;
	stress_asm_mb();
	vptr = (volatile uint64_t *)addr2;
	*vptr = pattern;

	if (UNLIKELY(verify)) {
		uint64_t val;

		vptr = (volatile uint64_t *)addr1;
		val = *vptr;
		if (UNLIKELY(val != pattern)) {
			pr_fail("%s: write: read back of stored value at address "
				"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
				args->name, vptr, pattern, val);
		}
		vptr = (volatile uint64_t *)addr2;
		val = *vptr;
		if (UNLIKELY(val != pattern)) {
			pr_fail("%s: write: read back of stored value at address "
				"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
				args->name, vptr, pattern, val);
		}
	}
}

/*
 *  hammer_write()
 *	write 64 bytes value to cache/memory
 */
static void OPTIMIZE3 hammer_write64(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	volatile uint64_t *vptr1, *vptr2;
	const uint64_t pattern = (uint64_t)0xaa55a55a55aa5aa5ULL;

	if (UNLIKELY(is_bad_addr))
		return;

	vptr1 = (volatile uint64_t *)addr1;
	vptr2 = (volatile uint64_t *)addr2;

	*(vptr1 + 0) = pattern;
	stress_asm_mb();
	*(vptr2 + 1) = pattern;
	stress_asm_mb();
	*(vptr1 + 2) = pattern;
	stress_asm_mb();
	*(vptr2 + 3) = pattern;
	stress_asm_mb();
	*(vptr1 + 4) = pattern;
	stress_asm_mb();
	*(vptr2 + 5) = pattern;
	stress_asm_mb();
	*(vptr1 + 6) = pattern;
	stress_asm_mb();
	*(vptr2 + 7) = pattern;
	stress_asm_mb();

	if (UNLIKELY(verify)) {
		size_t i;

		for (i = 0; i < 8; i++) {
			volatile uint64_t *vptr = (i & 1) ? (vptr2 + i) : (vptr1 + i);
			const uint64_t val = *vptr;

			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: write64: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr, pattern, val);
			}
		}
	}
}

/*
 *  hammer_read()
 *	read 64 bit value from cache/memory, write new value back to cache/memory
 */
static void OPTIMIZE3 hammer_readwrite(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	volatile uint64_t *vptr;
	const uint64_t pattern = (uint64_t)0x5aa555aaa555aaa5ULL;

	if (UNLIKELY(is_bad_addr))
		return;

	vptr = (volatile uint64_t *)addr1;
	(void)(*vptr);
	*vptr = pattern;
	stress_asm_mb();

	vptr = (volatile uint64_t *)addr2;
	(void)(*vptr);
	*vptr = pattern;
	stress_asm_mb();

	if (UNLIKELY(verify)) {
		uint64_t val;

		vptr = (volatile uint64_t *)addr1;
		val = *vptr;
		if (UNLIKELY(val != pattern)) {
			pr_fail("%s: readwrite: read back of stored value at address "
				"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
				args->name, vptr, pattern, val);
		}
		vptr = (volatile uint64_t *)addr2;
		val = *vptr;
		if (UNLIKELY(val != pattern)) {
			pr_fail("%s: readwrite: read back of stored value at address "
				"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
				args->name, vptr, pattern, val);
		}
	}
}

/*
 *  hammer_read()
 *	read 64 byte value from cache/memory, write new value back to cache/memory
 */
static void OPTIMIZE3 hammer_readwrite64(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	volatile uint64_t *vptr1, *vptr2;

	(void)args;
	(void)verify;

	if (UNLIKELY(is_bad_addr))
		return;

	vptr1 = (volatile uint64_t *)addr1;
	vptr2 = (volatile uint64_t *)addr2;

	*(vptr1 + 0);
	stress_asm_mb();
	*(vptr1 + 1) = 0;
	stress_asm_mb();
	*(vptr2 + 2);
	stress_asm_mb();
	*(vptr2 + 3) = 0;
	stress_asm_mb();
	*(vptr1 + 4);
	stress_asm_mb();
	*(vptr1 + 5) = 0;
	stress_asm_mb();
	*(vptr2 + 6);
	stress_asm_mb();
	*(vptr2 + 7) = 0;
	stress_asm_mb();
}

/*
 *  hammer_writeread()
 *	write 64 bit value to cache/memory, read it back from cache/memory
 */
static void OPTIMIZE3 hammer_writeread(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	volatile uint64_t *vptr;
	const uint64_t pattern = (uint64_t)0x5a5aa5a5aaaa5555ULL;

	if (UNLIKELY(is_bad_addr))
		return;

	vptr = (volatile uint64_t *)addr1;
	*vptr = pattern;
	(void)*vptr;
	stress_asm_mb();

	vptr = (volatile uint64_t *)addr2;
	*vptr = pattern;
	(void)*vptr;
	stress_asm_mb();

	if (UNLIKELY(verify)) {
		uint64_t val;

		vptr = (volatile uint64_t *)addr1;
		val = *vptr;
		if (UNLIKELY(val != pattern)) {
			pr_fail("%s: writeread: read back of stored value at address "
				"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
				args->name, vptr, pattern, val);
		}
		vptr = (volatile uint64_t *)addr2;
		val = *vptr;
		if (UNLIKELY(val != pattern)) {
			pr_fail("%s: writeread: read back of stored value at address "
				"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
				args->name, vptr, pattern, val);
		}
	}
}

/*
 *  hammer_writeread()
 *	write 64 byte value to cache/memory, read it back from cache/memory
 */
static void OPTIMIZE3 hammer_writeread64(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	volatile uint64_t *vptr1, *vptr2;
	const uint64_t pattern = (uint64_t)0xa5a55a5a5555aaaaULL;

	if (UNLIKELY(is_bad_addr))
		return;

	vptr1 = (volatile uint64_t *)addr1;
	vptr2 = (volatile uint64_t *)addr2;

	*(vptr1 + 0) = pattern;
	stress_asm_mb();
	*(vptr1 + 1);
	stress_asm_mb();
	*(vptr2 + 2) = pattern;
	stress_asm_mb();
	*(vptr2 + 3);
	stress_asm_mb();
	*(vptr1 + 4) = pattern;
	stress_asm_mb();
	*(vptr1 + 5);
	stress_asm_mb();
	*(vptr2 + 6) = pattern;
	stress_asm_mb();
	*(vptr2 + 7);
	stress_asm_mb();

	if (UNLIKELY(verify)) {
		size_t i;

		for (i = 0; i < 8; i += 2) {
			volatile uint64_t *vptr = (i & 2) ? (vptr2 + i) : (vptr1 + i);
			const uint64_t val = *vptr;

			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: writeread64: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr1, pattern, val);
			}
		}
	}
}

#if defined(HAVE_RISCV_CBO_ZERO)
static bool OPTIMIZE3 hammer_cbo_zero_valid(void)
{
	cpu_set_t cpus;
	struct riscv_hwprobe pair;

	(void)sched_getaffinity(0, sizeof(cpu_set_t), &cpus);

	pair.key = RISCV_HWPROBE_KEY_IMA_EXT_0;
	if (syscall(__NR_riscv_hwprobe, &pair, 1, sizeof(cpu_set_t), &cpus, 0) == 0) {
		if (pair.value & RISCV_HWPROBE_EXT_ZICBOZ) {
			pair.key = RISCV_HWPROBE_KEY_ZICBOZ_BLOCK_SIZE;

			if (syscall(__NR_riscv_hwprobe, &pair, 1,
				    sizeof(cpu_set_t), &cpus, 0) == 0) {
				return true;
			}
		}
	}
	return false;
}

/*
 *  hammer_cbo_zero
 *	RISC-V cache-based bzero
 */
static void OPTIMIZE3 hammer_cbo_zero(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)is_bad_addr;
	(void)verify;

	(void)stress_asm_riscv_cbo_zero((char *)addr1);
	stress_asm_mb();
	(void)stress_asm_riscv_cbo_zero((char *)addr2);
	stress_asm_mb();
}
#endif

#if defined(HAVE_BUILTIN___CLEAR_CACHE)
/*
 *  cache-based bzero
 *	generic gcc clear cache, clear a 64 byte cache line
 */
static void OPTIMIZE3 hammer_clearcache(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)is_bad_addr;
	(void)verify;

	__builtin___clear_cache((void *)addr1, (void *)((char *)addr1 + 64));
	stress_asm_mb();
	__builtin___clear_cache((void *)addr2, (void *)((char *)addr2 + 64));
	stress_asm_mb();
}
#endif

#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_ASM_PPC64_DCBST)
/*
 *  hammer_ppc64_dcbst()
 *	powerpc64 Data Cache Block Store
 */
static void OPTIMIZE3 hammer_ppc64_dcbst(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	if (UNLIKELY(is_bad_addr)) {
		stress_asm_ppc64_dcbst(addr1);
		stress_asm_mb();
		stress_asm_ppc64_dcbst(addr2);
		stress_asm_mb();
	} else {
		const uint64_t pattern = (uint64_t)0xaaaaa5a555555a5aULL;
		volatile uint64_t *vptr;

		vptr = (volatile uint64_t *)addr1;
		*vptr = pattern;
		stress_asm_mb();
		stress_asm_ppc64_dcbst(addr1);
		stress_asm_mb();

		vptr = (volatile uint64_t *)addr2;
		*vptr = pattern;
		stress_asm_mb();
		stress_asm_ppc64_dcbst(addr2);
		stress_asm_mb();

		if (UNLIKELY(verify)) {
			uint64_t val;

			vptr = (volatile uint64_t *)addr1;
			val = *vptr;
			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: dcbst: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr, pattern, val);
			}
			vptr = (volatile uint64_t *)addr2;
			val = *vptr;
			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: dcbst: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr, pattern, val);
			}
		}
	}
}
#endif

#if defined(STRESS_ARCH_PPC) &&	\
    defined(HAVE_ASM_PPC_DCBST)
/*
 *  hammer_ppc_dcbst()
 *	powerpc Data Cache Block Store
 */
static void OPTIMIZE3 hammer_ppc_dcbst(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	if (UNLIKELY(is_bad_addr)) {
		stress_asm_ppc_dcbst(addr1);
		stress_asm_mb();
		stress_asm_ppc_dcbst(addr2);
		stress_asm_mb();
	} else {
		const uint64_t pattern = (uint64_t)0x55555a5aaaaaa5a5ULL;
		volatile uint64_t *vptr;

		vptr = (volatile uint64_t *)addr1;
		*vptr = pattern;
		stress_asm_mb();
		stress_asm_ppc_dcbst(addr1);
		stress_asm_mb();

		vptr = (volatile uint64_t *)addr2;
		*vptr = pattern;
		stress_asm_mb();
		stress_asm_ppc_dcbst(addr2);
		stress_asm_mb();

		if (UNLIKELY(verify)) {
			uint64_t val;

			vptr = (volatile uint64_t *)addr1;
			val = *vptr;
			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: dcbst: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr, pattern, val);
			}
			vptr = (volatile uint64_t *)addr2;
			val = *vptr;
			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: dcbst: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr, pattern, val);
			}
		}
	}
}
#endif

#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_ASM_PPC64_DCBT)
/*
 *  hammer_ppc64_dcbt()
 *	powerpc64 Data Cache Block Touch
 */
static void OPTIMIZE3 hammer_ppc64_dcbt(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)verify;

	if (UNLIKELY(is_bad_addr)) {
		stress_asm_ppc64_dcbt(addr1);
		stress_asm_mb();
		stress_asm_ppc64_dcbt(addr2);
		stress_asm_mb();
	} else {
		const volatile uint64_t *vptr;

		vptr = (volatile uint64_t *)addr1;
		stress_asm_ppc64_dcbt(addr1);
		stress_asm_mb();
		(void)*vptr;
		stress_asm_mb();

		vptr = (volatile uint64_t *)addr2;
		stress_asm_ppc64_dcbt(addr2);
		stress_asm_mb();
		(void)*vptr;
		stress_asm_mb();
	}
}
#endif

#if defined(STRESS_ARCH_PPC) &&	\
    defined(HAVE_ASM_PPC_DCBT)
/*
 *  hammer_ppc_dcbt()
 *	powerpc Data Cache Block Touch
 */
static void OPTIMIZE3 hammer_ppc_dcbt(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)verify;

	if (UNLIKELY(is_bad_addr)) {
		stress_asm_ppc_dcbt(addr1);
		stress_asm_mb();
		stress_asm_ppc_dcbt(addr2);
		stress_asm_mb();
	} else {
		const volatile uint64_t *vptr;

		vptr = (volatile uint64_t *)addr1;
		stress_asm_ppc_dcbt(addr1);
		stress_asm_mb();
		(void)*vptr;
		stress_asm_mb();

		vptr = (volatile uint64_t *)addr2;
		stress_asm_ppc_dcbt(addr2);
		stress_asm_mb();
		(void)*vptr;
		stress_asm_mb();
	}
}
#endif

#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_ASM_PPC64_DCBTST)
/*
 *  hammer_ppc64_dcbtst()
 *	powerpc64 Data Cache Block Touch for Store
 */
static void OPTIMIZE3 hammer_ppc64_dcbtst(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	if (UNLIKELY(is_bad_addr)) {
		stress_asm_ppc64_dcbtst(addr1);
		stress_asm_mb();
		stress_asm_ppc64_dcbtst(addr2);
		stress_asm_mb();
	} else {
		const uint64_t pattern = (uint64_t)0x5aa5aa55a55a55aaULL;
		volatile uint64_t *vptr;

		vptr = (volatile uint64_t *)addr1;
		stress_asm_ppc64_dcbtst(addr1);
		stress_asm_mb();
		*vptr = pattern;
		stress_asm_mb();

		vptr = (volatile uint64_t *)addr2;
		stress_asm_ppc64_dcbtst(addr2);
		stress_asm_mb();
		*vptr = pattern;
		stress_asm_mb();

		if (UNLIKELY(verify)) {
			uint64_t val;

			vptr = (volatile uint64_t *)addr1;
			val = *vptr;
			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: dcbtst: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr, pattern, val);
			}
			vptr = (volatile uint64_t *)addr2;
			val = *vptr;
			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: dcbtst: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr, pattern, val);
			}
		}
	}
}
#endif

#if defined(STRESS_ARCH_PPC) &&	\
    defined(HAVE_ASM_PPC_DCBTST)
/*
 *  hammer_ppc_dcbtst()
 *	powerpc Data Cache Block Touch for Store
 */
static void OPTIMIZE3 hammer_ppc_dcbtst(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	if (UNLIKELY(is_bad_addr)) {
		stress_asm_ppc_dcbtst(addr1);
		stress_asm_mb();
		stress_asm_ppc_dcbtst(addr2);
		stress_asm_mb();
	} else {
		const uint64_t pattern = (uint64_t)0x5aa5aa55a55a55aaULL;
		volatile uint64_t *vptr;

		vptr = (volatile uint64_t *)addr1;
		stress_asm_ppc_dcbtst(addr1);
		stress_asm_mb();
		*vptr = pattern;
		stress_asm_mb();

		vptr = (volatile uint64_t *)addr2;
		stress_asm_ppc_dcbtst(addr2);
		stress_asm_mb();
		*vptr = pattern;
		stress_asm_mb();

		if (UNLIKELY(verify)) {
			uint64_t val;

			vptr = (volatile uint64_t *)addr1;
			val = *vptr;
			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: dcbtst: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr, pattern, val);
			}
			vptr = (volatile uint64_t *)addr2;
			val = *vptr;
			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: dcbtst: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr, pattern, val);
			}
		}
	}
}
#endif

#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_ASM_PPC64_MSYNC)
/*
 *  hammer_ppc64_msync()
 *	msync to memory
 */
static void OPTIMIZE3 hammer_ppc64_msync(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)verify;

	if (UNLIKELY(is_bad_addr)) {
		stress_asm_ppc64_msync();
		stress_asm_mb();
		stress_asm_ppc64_msync();
		stress_asm_mb();
	} else {
		volatile uint64_t *vptr;

		vptr = (volatile uint64_t *)addr1;
		*vptr = 0x0123456789abcdefULL;
		stress_asm_mb();
		stress_asm_ppc64_msync();
		stress_asm_mb();

		vptr = (volatile uint64_t *)addr2;
		*vptr = 0xfedcba9876543210ULL;
		stress_asm_mb();
		stress_asm_ppc64_msync();
		stress_asm_mb();
	}
}
#endif

/*
 *  hammer_prefetch()
 *	exercise gcc builtin prefetch, read/write and 4 levels of cache locality
 */
static void OPTIMIZE3 hammer_prefetch(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)verify;

	if (UNLIKELY(is_bad_addr)) {
		shim_builtin_prefetch(addr1, 0, 0);
		stress_asm_mb();
		shim_builtin_prefetch(addr2, 1, 0);
		stress_asm_mb();
		shim_builtin_prefetch(addr2, 0, 1);
		stress_asm_mb();
		shim_builtin_prefetch(addr1, 1, 1);
		stress_asm_mb();
		shim_builtin_prefetch(addr1, 0, 2);
		stress_asm_mb();
		shim_builtin_prefetch(addr2, 1, 2);
		stress_asm_mb();
		shim_builtin_prefetch(addr2, 0, 3);
		stress_asm_mb();
		shim_builtin_prefetch(addr1, 1, 3);
		stress_asm_mb();
	} else {
		/* issuing prefetch and then load close afterwards is suboptimal */
		shim_builtin_prefetch(addr1, 0, 0);
		stress_asm_mb();
		shim_builtin_prefetch(addr2, 1, 0);
		stress_asm_mb();

		*(volatile uint64_t *)addr1;
		stress_asm_mb();
		*(volatile uint64_t *)addr2;
		stress_asm_mb();

		shim_builtin_prefetch(addr2, 0, 1);
		stress_asm_mb();
		shim_builtin_prefetch(addr1, 1, 1);
		stress_asm_mb();

		*(volatile uint64_t *)addr2;
		stress_asm_mb();
		*(volatile uint64_t *)addr1;
		stress_asm_mb();

		shim_builtin_prefetch(addr1, 0, 2);
		stress_asm_mb();
		shim_builtin_prefetch(addr2, 1, 2);
		stress_asm_mb();

		*(volatile uint64_t *)addr1;
		stress_asm_mb();
		*(volatile uint64_t *)addr2;
		stress_asm_mb();

		shim_builtin_prefetch(addr2, 0, 3);
		stress_asm_mb();
		shim_builtin_prefetch(addr1, 1, 3);
		stress_asm_mb();

		*(volatile uint64_t *)addr2;
		stress_asm_mb();
		*(volatile uint64_t *)addr1;
		stress_asm_mb();
	}
}

#if defined(HAVE_ASM_X86_PREFETCHNTA)
/*
 *  hammer_prefetchnta()
 *	prefetch data into non-temporal cache structure and into a location
 *	close to the processor, minimizing cache pollution
 */
static void OPTIMIZE3 hammer_prefetchnta(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)is_bad_addr;
	(void)verify;

	if (UNLIKELY(is_bad_addr)) {
		stress_asm_x86_prefetchnta(addr1);
		stress_asm_mb();
		stress_asm_x86_prefetchnta(addr2);
		stress_asm_mb();
	} else {
		/* issuing prefetch and then load close afterwards is suboptimal */
		stress_asm_x86_prefetchnta(addr1);
		stress_asm_mb();
		stress_asm_x86_prefetchnta(addr2);
		stress_asm_mb();

		*(volatile uint64_t *)addr1;
		stress_asm_mb();
		*(volatile uint64_t *)addr2;
		stress_asm_mb();
	}
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHT0)
/*
 *  hammer_prefetcht0()
 *	prefetch data into all levels of the cache hierarchy
 */
static void OPTIMIZE3 hammer_prefetcht0(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)is_bad_addr;
	(void)verify;

	if (UNLIKELY(is_bad_addr)) {
		stress_asm_x86_prefetcht0(addr1);
		stress_asm_mb();
		stress_asm_x86_prefetcht0(addr2);
		stress_asm_mb();
	} else {
		/* issuing prefetch and then load close afterwards is suboptimal */
		stress_asm_x86_prefetcht0(addr1);
		stress_asm_mb();
		stress_asm_x86_prefetcht0(addr2);
		stress_asm_mb();

		*(volatile uint64_t *)addr1;
		stress_asm_mb();
		*(volatile uint64_t *)addr2;
		stress_asm_mb();
	}
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHT1)
/*
 *  hammer_prefetcht1()
 *	prefetch data into level 2 cache and higher
 */
static void OPTIMIZE3 hammer_prefetcht1(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)is_bad_addr;
	(void)verify;

	if (UNLIKELY(is_bad_addr)) {
		stress_asm_x86_prefetcht1(addr1);
		stress_asm_mb();
		stress_asm_x86_prefetcht1(addr2);
		stress_asm_mb();
	} else {
		/* issuing prefetch and then load close afterwards is suboptimal */
		stress_asm_x86_prefetcht1(addr1);
		stress_asm_mb();
		stress_asm_x86_prefetcht1(addr2);
		stress_asm_mb();

		*(volatile uint64_t *)addr1;
		stress_asm_mb();
		*(volatile uint64_t *)addr2;
		stress_asm_mb();
	}
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHT2)
/*
 *  hammer_prefetcht2()
 *	refetch data into level 3 cache and higher, or an implementation-specific choice
 */
static void OPTIMIZE3 hammer_prefetcht2(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)is_bad_addr;
	(void)verify;

	if (UNLIKELY(is_bad_addr)) {
		stress_asm_x86_prefetcht2(addr1);
		stress_asm_mb();
		stress_asm_x86_prefetcht2(addr2);
		stress_asm_mb();
	} else {
		/* issuing prefetch and then load close afterwards is suboptimal */
		stress_asm_x86_prefetcht2(addr1);
		stress_asm_mb();
		stress_asm_x86_prefetcht2(addr2);
		stress_asm_mb();

		*(volatile uint64_t *)addr1;
		stress_asm_mb();
		*(volatile uint64_t *)addr2;
		stress_asm_mb();
	}
}
#endif

/*
 *  hammer_prefetch_read()
 *	prefetch for reading, then do stores
 */
static void OPTIMIZE3 hammer_prefetch_read(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)verify;

	if (UNLIKELY(is_bad_addr)) {
		shim_builtin_prefetch(addr1, 0, 0);
		stress_asm_mb();
		shim_builtin_prefetch(addr2, 0, 0);
		stress_asm_mb();
	} else {
		/* issuing prefetch and then load close afterwards is suboptimal */
		shim_builtin_prefetch(addr1, 0, 0);
		stress_asm_mb();
		shim_builtin_prefetch(addr2, 0, 0);
		stress_asm_mb();

		*(volatile uint64_t *)addr1;
		stress_asm_mb();
		*(volatile uint64_t *)addr2;
		stress_asm_mb();
	}
}

#if defined(HAVE_ASM_X86_CLDEMOTE)
/*
 *  hammer_cldemote()
 *	x86 cache line demote
 */
static void OPTIMIZE3 hammer_cldemote(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)is_bad_addr;
	(void)verify;

	if (UNLIKELY(is_bad_addr)) {
		stress_asm_x86_cldemote(addr1);
		stress_asm_mb();
		stress_asm_x86_cldemote(addr2);
		stress_asm_mb();
	} else {
		stress_asm_x86_cldemote(addr1);
		stress_asm_mb();
		stress_asm_x86_cldemote(addr2);
		stress_asm_mb();

		*(volatile uint64_t *)addr1;
		stress_asm_mb();
		*(volatile uint64_t *)addr2;
		stress_asm_mb();
		*(volatile uint64_t *)addr1 = 0;
		stress_asm_mb();
		*(volatile uint64_t *)addr2 = 0;
		stress_asm_mb();
	}
}
#endif

#if defined(HAVE_ASM_X86_CLFLUSH)
/*
 *  hammer_clflush()
 *	x86 cache line flush
 */
static void OPTIMIZE3 hammer_clflush(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)is_bad_addr;
	(void)verify;

	if (UNLIKELY(is_bad_addr)) {
		stress_asm_x86_clflush(addr1);
		stress_asm_mb();
		stress_asm_x86_clflush(addr2);
		stress_asm_mb();
	} else {
		stress_asm_x86_clflush(addr1);
		stress_asm_mb();
		stress_asm_x86_clflush(addr2);
		stress_asm_mb();

		*(volatile uint64_t *)addr1;
		stress_asm_mb();
		*(volatile uint64_t *)addr2;
		stress_asm_mb();
		*(volatile uint64_t *)addr1 = 0;
		stress_asm_mb();
		*(volatile uint64_t *)addr2 = 0;
		stress_asm_mb();
	}
}
#endif

#if defined(HAVE_ASM_X86_CLFLUSH)
/*
 *  hammer_write_clflush
 *	x86 write followed by cache line flush
 */
static void OPTIMIZE3 hammer_write_clflush(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	if (UNLIKELY(is_bad_addr)) {
		stress_asm_x86_clflush(addr1);
		stress_asm_mb();
		stress_asm_x86_clflush(addr2);
		stress_asm_mb();
	} else {
		volatile uint64_t *vptr;
		const uint64_t pattern = (uint64_t)0xaaaaa5a555555a5aULL;

		vptr = (volatile uint64_t *)addr1;
		*vptr = pattern;
		stress_asm_mb();
		stress_asm_x86_clflush(addr1);
		stress_asm_mb();
		vptr = (volatile uint64_t *)addr2;
		*vptr = pattern;
		stress_asm_mb();
		stress_asm_x86_clflush(addr2);
		stress_asm_mb();

		if (UNLIKELY(verify)) {
			uint64_t val;

			vptr = (volatile uint64_t *)addr1;
			val = *vptr;
			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: write-clflush: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr, pattern, val);
			}
			vptr = (volatile uint64_t *)addr2;
			val = *vptr;
			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: write-clflush: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr, pattern, val);
			}
		}
	}
}
#endif

#if defined(HAVE_ASM_X86_CLFLUSHOPT)
/*
 *  hammer_write_clflush
 *	x86 write followed by optimized cache line flush
 */
static void OPTIMIZE3 hammer_write_clflushopt(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	if (UNLIKELY(is_bad_addr)) {
		stress_asm_x86_clflushopt(addr1);
		stress_asm_mb();
		stress_asm_x86_clflushopt(addr2);
		stress_asm_mb();
	} else {
		volatile uint64_t *vptr;
		const uint64_t pattern = (uint64_t)0x55555a5aaaaaa5a5ULL;

		vptr = (volatile uint64_t *)addr1;
		*vptr = pattern;
		stress_asm_mb();
		stress_asm_x86_clflushopt(addr1);
		stress_asm_mb();
		vptr = (volatile uint64_t *)addr2;
		*vptr = pattern;
		stress_asm_mb();
		stress_asm_x86_clflushopt(addr2);
		stress_asm_mb();

		if (UNLIKELY(verify)) {
			uint64_t val;

			vptr = (volatile uint64_t *)addr1;
			val = *vptr;
			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: write-clflushopt: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr, pattern, val);
			}
			vptr = (volatile uint64_t *)addr2;
			val = *vptr;
			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: write-clflushopt: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr, pattern, val);
			}
		}
	}
}
#endif


#if defined(HAVE_ASM_X86_CLFLUSHOPT)
/*
 *  hammer_clflush()
 *	x86 cache line flush
 */
static void OPTIMIZE3 hammer_clflushopt(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)is_bad_addr;
	(void)verify;

	stress_asm_x86_clflushopt(addr1);
	stress_asm_mb();
	stress_asm_x86_clflushopt(addr2);
	stress_asm_mb();
}
#endif

#if defined(HAVE_ASM_X86_CLWB)
/*
 *  hammer_clwb()
 *	x86 cache line write-back, dirty cache line and write back
 */
static void OPTIMIZE3 hammer_clwb(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)is_bad_addr;

	if (UNLIKELY(is_bad_addr)) {
		stress_asm_x86_clwb(addr1);
		stress_asm_mb();
		stress_asm_x86_clwb(addr2);
		stress_asm_mb();
	} else {
		volatile uint64_t *vptr;
		const uint64_t pattern = (uint64_t)0x55aa5aa5aa55a5a5ULL;

		vptr = (volatile uint64_t *)addr1;
		*vptr = pattern;
		stress_asm_mb();
		vptr = (volatile uint64_t *)addr2;
		*vptr = pattern;
		stress_asm_mb();

		stress_asm_x86_clwb(addr1);
		stress_asm_mb();
		stress_asm_x86_clwb(addr2);
		stress_asm_mb();

		if (UNLIKELY(verify)) {
			uint64_t val;

			vptr = (volatile uint64_t *)addr1;
			val = *vptr;
			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: write-clwb: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr, pattern, val);
			}
			vptr = (volatile uint64_t *)addr2;
			val = *vptr;
			if (UNLIKELY(val != pattern)) {
				pr_fail("%s: write-clwb: read back of stored value at address "
					"%p not %" PRIx64 ", got %" PRIx64 " instead\n",
					args->name, vptr, pattern, val);
			}
		}
	}
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHW)
/*
 *  hammer_prefetchw()
 *	x86 prefetch with anticipation of following write
 */
static void OPTIMIZE3 hammer_prefetchw(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)is_bad_addr;
	(void)verify;

	if (UNLIKELY(is_bad_addr)) {
		stress_asm_x86_prefetchw(addr1);
		stress_asm_mb();
		stress_asm_x86_prefetchw(addr2);
		stress_asm_mb();
	} else {
		/* issuing prefetch and then load close afterwards is suboptimal */
		stress_asm_x86_prefetchw(addr1);
		stress_asm_mb();
		stress_asm_x86_prefetchw(addr2);
		stress_asm_mb();

		*(volatile uint64_t *)addr1;
		stress_asm_mb();
		*(volatile uint64_t *)addr2;
		stress_asm_mb();
	}
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHWT1)
/*
 *  hammer_prefetchw()
 *	x86 prefetch vector data into cache with intent to write and T1 Hint
 */
static void OPTIMIZE3 hammer_prefetchwt1(
	stress_args_t *args,
	void *addr1,
	void *addr2,
	const bool is_bad_addr,
	const bool verify)
{
	(void)args;
	(void)is_bad_addr;
	(void)verify;

	if (UNLIKELY(is_bad_addr)) {
		stress_asm_x86_prefetchwt1(addr1);
		stress_asm_mb();
		stress_asm_x86_prefetchwt1(addr2);
		stress_asm_mb();
	} else {
		/* issuing prefetch and then load close afterwards is suboptimal */
		stress_asm_x86_prefetchwt1(addr1);
		stress_asm_mb();
		stress_asm_x86_prefetchwt1(addr2);
		stress_asm_mb();

		*(volatile uint64_t *)addr1;
		stress_asm_mb();
		*(volatile uint64_t *)addr2;
		stress_asm_mb();
	}
}
#endif

static const stress_cachehammer_func_t stress_cachehammer_funcs[] = {
#if defined(HAVE_RISCV_CBO_ZERO)
	{ "cbo_zero",	hammer_cbo_zero_valid,		hammer_cbo_zero },
#endif
#if defined(HAVE_ASM_X86_CLDEMOTE)
	{ "cldemote",	stress_cpu_x86_has_cldemote,	hammer_cldemote },
#endif
#if defined(HAVE_BUILTIN___CLEAR_CACHE)
	{ "clearcache",	hammer_valid,			hammer_clearcache },
#endif
#if defined(HAVE_ASM_X86_CLFLUSH)
	{ "clflush",	stress_cpu_x86_has_clfsh,	hammer_clflush },
#endif
#if defined(HAVE_ASM_X86_CLFLUSHOPT)
	{ "clflushopt",	stress_cpu_x86_has_clflushopt,	hammer_clflushopt },
#endif
#if defined(HAVE_ASM_X86_CLWB)
	{ "clwb",	stress_cpu_x86_has_clwb,	hammer_clwb },
#endif
#if defined(STRESS_ARCH_PPC) &&	\
    defined(HAVE_ASM_PPC_DCBST)
	{ "dcbst",	hammer_valid,			hammer_ppc_dcbst },
#endif
#if defined(STRESS_ARCH_PPC) &&	\
    defined(HAVE_ASM_PPC_DCBT)
	{ "dcbt",	hammer_valid,			hammer_ppc_dcbt },
#endif
#if defined(STRESS_ARCH_PPC) &&	\
    defined(HAVE_ASM_PPC_DCBTST)
	{ "dcbtst",	hammer_valid,			hammer_ppc_dcbtst },
#endif
#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_ASM_PPC64_DCBST)
	{ "dcbst",	hammer_valid,			hammer_ppc64_dcbst },
#endif
#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_ASM_PPC64_DCBT)
	{ "dcbt",	hammer_valid,			hammer_ppc64_dcbt },
#endif
#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_ASM_PPC64_DCBTST)
	{ "dcbtst",	hammer_valid,			hammer_ppc64_dcbtst },
#endif
#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_ASM_PPC64_MSYNC)
	{ "msync",	hammer_valid,			hammer_ppc64_msync },
#endif
	{ "prefetch", 	hammer_valid,			hammer_prefetch },
#if defined(HAVE_ASM_X86_PREFETCHNTA)
	{ "prefetchnta", stress_cpu_x86_has_sse,	hammer_prefetchnta },
#endif
#if defined(HAVE_ASM_X86_PREFETCHT0)
	{ "prefetcht0", stress_cpu_x86_has_sse,		hammer_prefetcht0 },
#endif
#if defined(HAVE_ASM_X86_PREFETCHT1)
	{ "prefetcht1", stress_cpu_x86_has_sse,		hammer_prefetcht1 },
#endif
#if defined(HAVE_ASM_X86_PREFETCHT2)
	{ "prefetcht2", stress_cpu_x86_has_sse,		hammer_prefetcht2 },
#endif
#if defined(HAVE_ASM_X86_PREFETCHW)
	{ "prefetchw",	stress_cpu_x86_has_sse,		hammer_prefetchw },
#endif
#if defined(HAVE_ASM_X86_PREFETCHWT1)
	{ "prefetchwt1", stress_cpu_x86_has_prefetchwt1, hammer_prefetchwt1 },
#endif
	{ "prefetch-read", hammer_valid,		hammer_prefetch_read },
	{ "read",	hammer_valid,			hammer_read },
	{ "read64",	hammer_valid,			hammer_read64 },
	{ "read-write",	hammer_valid,			hammer_readwrite },
	{ "read-write64", hammer_valid,			hammer_readwrite64 },
	{ "write",	hammer_valid,			hammer_write },
#if defined(HAVE_ASM_X86_CLFLUSH)
	{ "write-clflush", stress_cpu_x86_has_clfsh,	hammer_write_clflush },
#endif
#if defined(HAVE_ASM_X86_CLFLUSHOPT)
	{ "write-clflushopt", stress_cpu_x86_has_clflushopt, hammer_write_clflushopt },
#endif
	{ "write64",	hammer_valid,			hammer_write64 },
	{ "write-read",	hammer_valid,			hammer_writeread },
	{ "write-read64", hammer_valid,			hammer_writeread64 },
};

static stress_metrics_t cachehammer_metrics[N_FUNCS];
static bool valid[N_FUNCS];
static bool trapped[N_FUNCS];
static size_t func_index;

static void NORETURN MLOCKED_TEXT stress_cache_sighandler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);         /* Ugly, bounce back */
	stress_no_return();
}

/*
 *  stress_cache_hammer_flags_to_str()
 *	turn set flags into corresponding name of flags
 */
static void stress_cache_hammer_flags_to_str(
	char *buf,
	size_t buf_len,
	const bool flags[N_FUNCS])
{
	char *ptr = buf;
	size_t i;

	(void)shim_memset(buf, 0, buf_len);
	for (i = 0; i < N_FUNCS; i++) {
		if (flags[i]) {
			const char *name = stress_cachehammer_funcs[i].name;
			const size_t len = strlen(name);

			(void)shim_strscpy(ptr, " ", buf_len);
			buf_len--;
			ptr++;

			(void)shim_strscpy(ptr, name, buf_len);
			buf_len -= len;
			ptr += len;
		}
	}
	*ptr = '\0';
}

#if defined(HAVE_LINUX_MEMPOLICY_H)
static inline void stress_cachehammer_numa(
	stress_args_t *args,
	const int max,
	int *numa_count,
	void *addr,
	const bool cachehammer_numa,
        stress_numa_mask_t *numa_mask,
        stress_numa_mask_t *numa_nodes)
{
	if (cachehammer_numa && numa_mask && numa_nodes) {
		(*numa_count)++;
		if (*numa_count > max) {
			const size_t page_size = args->page_size;

			/* page align addr */
			addr = (void *)((uintptr_t)addr & ~(page_size - 1));
			stress_numa_randomize_pages(args, numa_nodes,
					numa_mask, addr, page_size, page_size);
			*numa_count = 0;
		}
	}
}
#endif

/*
 *  stress_cachehammer
 *	stress cache by psuedo-random memory read/writes and
 *	if possible change CPU affinity to try to cause
 *	poor cache behaviour
 */
static int OPTIMIZE3 stress_cachehammer(stress_args_t *args)
{
	NOCLOBBER int ret = EXIT_SUCCESS, fd;
	NOCLOBBER uint8_t *local_buffer, *local_page, *file_page, *bad_page;
	uint8_t *const buffer = g_shared->mem_cache.buffer;
	const size_t page_size = args->page_size;
	const size_t buffer_size = (size_t)g_shared->mem_cache.size;
	const size_t local_buffer_size = buffer_size * 4;
	const uint32_t mask = ~0x3f;
	const uint32_t page_mask = (page_size - 1) & ~0x3f;
	size_t i, j;
	NOCLOBBER size_t tries = 0;
	char buf[1024];
	NOCLOBBER bool cachehammer_numa = false;
	static int numa_count[5];
	double mantissa;
	uint64_t exponent;
#if defined(HAVE_LINUX_MEMPOLICY_H)
	NOCLOBBER stress_numa_mask_t *numa_mask;
	NOCLOBBER stress_numa_mask_t *numa_nodes;

	numa_mask = NULL;
	numa_nodes = NULL;
#endif

	(void)shim_memset(numa_count, 0, sizeof(numa_count));
	(void)stress_get_setting("cachehammer-numa", &cachehammer_numa);

	if (!*cachehammer_filename) {
		pr_inf_skip("%s: shared file not created, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}
	func_index = 0;
	for (i = 0; i < N_FUNCS; i++) {
		valid[i] = stress_cachehammer_funcs[i].valid();
		trapped[i] = false;
		cachehammer_metrics[i].duration = 0.0;
		cachehammer_metrics[i].count = 0.0;
	}

	if (stress_instance_zero(args)) {
		pr_dbg("%s: using cache buffer size of %zuK\n",
			args->name, buffer_size / 1024);
		stress_cache_hammer_flags_to_str(buf, sizeof(buf), valid);
		if (*buf)
			pr_inf("%s: using operations:%s\n", args->name, buf);
	}

	if (sigsetjmp(jmp_env, 1)) {
		pr_inf_skip("%s: premature SIGSEGV caught, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}

	if (stress_sighandler(args->name, SIGSEGV, stress_cache_sighandler, NULL) < 0)
		return EXIT_NO_RESOURCE;
	if (stress_sighandler(args->name, SIGBUS, stress_cache_sighandler, NULL) < 0)
		return EXIT_NO_RESOURCE;
	if (stress_sighandler(args->name, SIGILL, stress_cache_sighandler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	/*
	 *  map a page then unmap it, then we have an address
	 *  that is known to be not available. If the mapping
	 *  fails we have MAP_FAILED which too is an invalid
	 *  bad address.
	 */
	bad_page = (uint8_t *)mmap(NULL, page_size, PROT_READ,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (bad_page == MAP_FAILED)
		bad_page = buffer;	/* use something */
	else
		(void)munmap((void *)bad_page, page_size);

	local_buffer = (uint8_t *)mmap(NULL, local_buffer_size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (local_buffer == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zu bytes%s, skipping stressor\n",
			args->name, local_buffer_size,
			stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	local_page = (uint8_t *)mmap(NULL, page_size, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (local_page == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zu bytes%s, skipping stressor\n",
			args->name, page_size,
			stress_get_memfree_str());
		ret = EXIT_NO_RESOURCE;
		goto unmap_local_buffer;
	}

	/*
	 *  file_page in should have the same physical address across all the
	 *  cachehammer instances so this may impact snooping performance
	 */
	fd = open(cachehammer_filename, O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		pr_inf_skip("%s: cannot open file '%s', errno=%d (%s), skipping stressor\n",
			args->name, cachehammer_filename, errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto unmap_local_page;
	}
	file_page = (uint8_t *)mmap(NULL, page_size, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);
	if (file_page == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zu bytes%s, skipping stressor\n",
			args->name, page_size, stress_get_memfree_str());
		ret = EXIT_NO_RESOURCE;
		goto close_fd;
	}

	if (cachehammer_numa) {
#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_and_node_alloc(args, &numa_nodes, &numa_mask, "--cachehammer-numa", &cachehammer_numa);
#else
		if (stress_instance_zero(args))
			pr_inf("%s: --cachehammer numa selected but not supported by this system, disabling option\n",
				args->name);
		cachehammer_numa = false;
#endif
	}

	(void)shim_memset(buffer, 0, buffer_size);
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (cachehammer_numa) {
#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_and_node_alloc(args, &numa_nodes, &numa_mask,
						"--cachehammer-numa", &cachehammer_numa);
#else
		if (stress_instance_zero(args))
			pr_inf("%s: --cachehammer-numa selected but not supported by this system, disabling option\n",
				args->name);
		cachehammer_numa = false;
#endif
	}

	(void)sigsetjmp(jmp_env, 1);
	func_index = stress_mwc32modn((uint32_t)N_FUNCS);
	while (stress_continue(args)) {
		if (valid[func_index] && !trapped[func_index]) {
			double t_start;
			const uint16_t rnd16 = stress_mwc16();
			const size_t loops = 8 + ((rnd16 >> 1) & 0x3f);
			const uint8_t which = (rnd16 == 0x0008) ? 4 : rnd16 & 3;
			register hammer_func_t hammer = stress_cachehammer_funcs[func_index].hammer;

			uint32_t offset;
			register uint8_t *addr1, *addr2;

			t_start = stress_time_now();
			switch (which) {
			case 0:
				(*file_page)++;
#if defined(HAVE_MSYNC)
				/*
				 *  intentionally hit same page and
				 *  cache line each time
				 */
				if (UNLIKELY((rnd16 == 0x0020) && SIZEOF_ARRAY(msync_flags) > 0)) {
					const int flag = msync_flags[stress_mwc8modn(SIZEOF_ARRAY(msync_flags))];

					(void)msync((void *)file_page, page_size, flag);
				}
#endif
#if defined(HAVE_LINUX_MEMPOLICY_H)
				stress_cachehammer_numa(args, 50, &numa_count[0], file_page,
							cachehammer_numa, numa_mask, numa_nodes);
#endif
				hammer(args, file_page, file_page + 64, false, false);
				break;
			case 1:
			default:
				offset = stress_mwc32modn((uint32_t)buffer_size);
				addr1 = buffer + (offset & mask);
				addr2 = addr1;

#if defined(HAVE_LINUX_MEMPOLICY_H)
				stress_cachehammer_numa(args, 20, &numa_count[1], addr1,
							cachehammer_numa, numa_mask, numa_nodes);
#endif
				for (i = 0; i < loops; i++) {
					addr2 += 64;
					if (UNLIKELY(addr2 >= buffer + buffer_size))
						addr2 = buffer;
					hammer(args, addr1, addr2, false, false);
					hammer(args, addr2, addr1, false, false);
				}
				break;
			case 2:
				offset = stress_mwc32modn((uint32_t)local_buffer_size);
				addr1 = local_buffer + (offset & mask);
				addr2 = addr1;

#if defined(HAVE_LINUX_MEMPOLICY_H)
				stress_cachehammer_numa(args, 20, &numa_count[2], addr1,
							cachehammer_numa, numa_mask, numa_nodes);
#endif
				for (i = 0; i < loops; i++) {
					addr2 += 64;
					if (UNLIKELY(addr2 >= local_buffer + local_buffer_size))
						addr2 = local_buffer;
					hammer(args, addr1, addr2, false, true);
					hammer(args, addr2, addr1, false, true);
				}
				break;
			case 3:
				offset = stress_mwc32modn((uint32_t)page_size);
				addr1 = local_page + (offset & page_mask);
				addr2 = addr1;

#if defined(HAVE_LINUX_MEMPOLICY_H)
				stress_cachehammer_numa(args, 20, &numa_count[3], addr1,
							cachehammer_numa, numa_mask, numa_nodes);
#endif
				for (i = 0; i < loops; i++) {
					addr2 += 64;
					if (UNLIKELY(addr2 >= local_page + page_size))
						addr2 = local_page;
					hammer(args, addr1, addr2, false, true);
					hammer(args, addr2, addr1, false, true);
				}
				break;
			case 4:
				offset = stress_mwc16();
				addr1 = bad_page + (offset & page_mask);
				offset += 64;
				addr2 = bad_page + (offset & page_mask);

#if defined(HAVE_LINUX_MEMPOLICY_H)
				stress_cachehammer_numa(args, 50, &numa_count[4], addr1,
							cachehammer_numa, numa_mask, numa_nodes);
#endif
				hammer(args, addr1, addr2, true, false);
				break;
			}
			cachehammer_metrics[func_index].duration += stress_time_now() - t_start;
			cachehammer_metrics[func_index].count += 1.0;
			tries = 0;
			stress_bogo_inc(args);
			func_index = stress_mwc32modn((uint32_t)N_FUNCS);
		} else {
			tries++;
			if (UNLIKELY(tries > N_FUNCS)) {
				pr_inf("%s: terminating early, cannot invoke any valid cache operations\n",
					args->name);
				break;
			}
			func_index++;
			if (UNLIKELY(func_index >= N_FUNCS))
				func_index = 0;
		}
	}

	/*
	 *  Hit an illegal instruction? report the disabled flags
	 */
	if (stress_instance_zero(args)) {
		stress_cache_hammer_flags_to_str(buf, sizeof(buf), trapped);
		if (*buf)
			pr_inf("%s: disabled%s due to SIGBUS/SEGV/SIGILL\n", args->name, buf);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	mantissa = 1.0;
	exponent = 0;

	for (i = 0, j = 0; i < N_FUNCS; i++) {
		if (cachehammer_metrics[i].duration > 0.0) {
			char msg[64];
			int e;
			const double rate = cachehammer_metrics[i].count / cachehammer_metrics[i].duration;
			const double f = frexp(rate, &e);

			mantissa *= f;
			exponent += e;

			(void)snprintf(msg, sizeof(msg), "%s cache bogo-ops/sec",
				 stress_cachehammer_funcs[i].name);
			stress_metrics_set(args, j, msg, rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}

	if (j > 0) {
		double inverse_n = 1.0 / (double)j;
		double geomean = pow(mantissa, inverse_n) *
				 pow(2.0, (double)exponent * inverse_n);

		pr_dbg("%s: %.2f cachehammer ops per second (geometric mean of per stressor bogo-op rates)\n",
			args->name, geomean);
	}

#if defined(HAVE_LINUX_MEMPOLICY_H)
        if (numa_mask)
                stress_numa_mask_free(numa_mask);
        if (numa_nodes)
                stress_numa_mask_free(numa_nodes);
#endif
	(void)munmap((void *)file_page, page_size);
close_fd:
	(void)close(fd);
unmap_local_page:
	(void)munmap((void *)local_page, page_size);
unmap_local_buffer:
	(void)munmap((void *)local_buffer, local_buffer_size);
	return ret;
}

const stressor_info_t stress_cachehammer_info = {
	.stressor = stress_cachehammer,
	.init = stress_cachehammer_init,
	.deinit = stress_cachehammer_deinit,
	.classifier = CLASS_CPU_CACHE,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help
};

#else

const stressor_info_t stress_cachehammer_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU_CACHE,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without siglongjmp support"
};

#endif
