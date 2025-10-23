/*
 * Copyright (C) 2016-2017 Intel, Ltd.
 * Copyright (C) 2016-2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King.
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
#include "core-asm-x86.h"
#include "core-arch.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"

#include <ctype.h>

#if defined(HAVE_SYS_AUXV_H)
#include <sys/auxv.h>
#endif

typedef enum {
	STRESS_CACHE_SIZE,
	STRESS_CACHE_LINE_SIZE,
	STRESS_CACHE_WAYS
} cache_size_type_t;

#if defined(__linux__)
static const char stress_sys_cpu_prefix[] = "/sys/devices/system/cpu";
static const char stress_cpu_cache_dir[] = "cache";
#endif

/*
 * stress_cpu_cache_get_cpu()
 *
 */
static inline unsigned int stress_cpu_cache_get_cpu(const stress_cpu_cache_cpus_t *cpus)
{
	const unsigned int cpu = stress_get_cpu();

	return (cpu >= cpus->count) ? 0 : cpu;
}

#if defined(__linux__)
/*
 * stress_get_string_from_file()
 * 	read data from file into a fixed size buffer
 *	and remove any trailing newlines
 */
static int stress_get_string_from_file(
	const char *path,
	char *tmp,
	const size_t tmp_len)
{
	char *ptr;
	ssize_t ret;

	/* system read will zero fill tmp */
	ret = stress_system_read(path, tmp, tmp_len);
	if (UNLIKELY(ret < 0))
		return -1;

	ptr = strchr(tmp, '\n');
	if (ptr)
		*ptr = '\0';

	return 0;
}
#endif

/*
 * stress_cpu_cache_get_by_cpu()
 * @cpu: cpu to consider.
 * @cache_level: numeric cache level (1-indexed).
 * Obtain the cpu cache indexed by @cache_level.
 *
 * POTENTIAL BUG: assumes only 1 data cache per CPU cache level.
 *
 * Returns: stress_cpu_cache_t, or NULL on error.
 */
static stress_cpu_cache_t * stress_cpu_cache_get_by_cpu(
	const stress_cpu_cache_cpu_t *cpu,
	const int cache_level)
{
	uint32_t  i;

	if (UNLIKELY(!cpu || !cache_level))
		return NULL;

	for (i = 0; i < cpu->cache_count; i++) {
		stress_cpu_cache_t *p = &cpu->caches[i];

		if (p->level != cache_level)
			continue;

		/* we want a data cache */
		if (p->type != CACHE_TYPE_INSTRUCTION)
			return p;
	}
	return NULL;
}

/*
 * stress_cpu_cache_get_max_level()
 * @cpus: array of cpus to query.
 * Determine the maximum cache level available on the system.
 *
 * Returns: 1-index value denoting highest cache level, or 0 on error.
 */
uint16_t stress_cpu_cache_get_max_level(const stress_cpu_cache_cpus_t *cpus)
{
	stress_cpu_cache_cpu_t    *cpu;
	uint32_t  i;
	uint16_t  max = 0;

	if (UNLIKELY(!cpus)) {
		pr_dbg("%s: invalid cpus parameter\n", __func__);
		return 0;
	}

	cpu = &cpus->cpus[stress_cpu_cache_get_cpu(cpus)];

	for (i = 0; i < cpu->cache_count; i++) {
		const stress_cpu_cache_t *cache = &cpu->caches[i];

		max = cache->level > max ? cache->level : max;
	}

	return max;
}

/*
 * stress_cpu_cache_get()
 * @cpus: array of cpus to query.
 * @cache_level: numeric cache level (1-indexed).
 * Obtain a cpu cache of level @cache_level.
 *
 * Returns: stress_cpu_cache_t pointer, or NULL on error.
 */
stress_cpu_cache_t *stress_cpu_cache_get(const stress_cpu_cache_cpus_t *cpus, const uint16_t cache_level)
{
	const stress_cpu_cache_cpu_t *cpu;

	if (UNLIKELY(!cpus)) {
		pr_dbg("%s: invalid cpus parameter\n", __func__);
		return NULL;
	}

	if (UNLIKELY(!cache_level)) {
		pr_dbg("%s: invalid cache_level: %d\n",
			__func__, cache_level);
		return NULL;
	}

	cpu = &cpus->cpus[stress_cpu_cache_get_cpu(cpus)];

	return stress_cpu_cache_get_by_cpu(cpu, cache_level);
}

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_SPARC)
static int stress_cpu_cache_get_value(
	const char *cpu_path,
	const char *file,
	uint64_t *value)
{
	char path[PATH_MAX];
	char tmp[128];

	(void)stress_mk_filename(path, sizeof(path), cpu_path, file);
	if (LIKELY(stress_get_string_from_file(path, tmp, sizeof(tmp)) == 0)) {
		if (sscanf(tmp, "%" SCNu64, value) == 1)
			return 0;
	}
	return -1;
}
#endif

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_ALPHA)
/*
 *  stress_cpu_cache_get_alpha()
 *	find cache information as provided by linux Alpha from
 *	/proc/cpu. Assume cache layout for 1st CPU is same for
 *	all CPUs.
 */
static int stress_cpu_cache_get_alpha(
	stress_cpu_cache_cpu_t *cpu,
	const char *cpu_path)
{
	FILE *fp;
	const size_t count = 4;
	size_t idx = 0;

	(void)cpu_path;

	/*
	 * parse /proc/cpu info in the form:
	 * L1 Icache		: 64K, 2-way, 64b line
	 * L1 Dcache		: 64K, 2-way, 64b line
	 * L2 cache		: n/a
	 * L3 cache		: n/a
	 */
	cpu->caches = (stress_cpu_cache_t *)calloc(count, sizeof(*(cpu->caches)));
	if (UNLIKELY(!cpu->caches)) {
		pr_err("failed to allocate %zu bytes for cpu caches\n",
			count * sizeof(*(cpu->caches)));
		return 0;
	}

	fp = fopen("/proc/cpuinfo", "r");
	if (fp) {
		char buffer[4096];

		while ((idx < count) && fgets(buffer, sizeof(buffer), fp)) {
			stress_cpu_cache_type_t cache_type = CACHE_TYPE_UNKNOWN;
			uint16_t cache_level = 0;
			const char *ptr;
			uint64_t cache_size;
			int cache_ways, cache_line_size, n;

			if (!strncmp("L1 Icache", buffer, 9)) {
				cache_type = CACHE_TYPE_INSTRUCTION;
				cache_level = 1;
			} else if (!strncmp("L1 Dcache", buffer, 9))  {
				cache_type = CACHE_TYPE_DATA;
				cache_level = 1;
			} else if (!strncmp("L2 cache", buffer, 8)) {
				cache_type = CACHE_TYPE_DATA;
				cache_level = 2;
			} else if (!strncmp("L3 cache", buffer, 8)) {
				cache_type = CACHE_TYPE_DATA;
				cache_level = 3;
			} else {
				continue;
			}
			ptr = strchr(buffer, ':');
			if (!ptr)
				continue;
			ptr++;
			cache_size = 0;
			cache_ways = 0;
			cache_line_size = 0;
			n = sscanf(ptr, "%" SCNu64 "K, %d-way, %db line",
				&cache_size, &cache_ways, &cache_line_size);
			if (n != 3)
				continue;
			cpu->caches[idx].type = cache_type;
			cpu->caches[idx].level = cache_level;
			cpu->caches[idx].size = cache_size * 1024;
			cpu->caches[idx].ways = cache_ways;
			cpu->caches[idx].line_size = cache_line_size;
			idx++;
		}
		(void)fclose(fp);
	}

	if (idx == 0) {
		free(cpu->caches);
		cpu->caches = NULL;
		cpu->cache_count = 0;

		return 0;
	}
	cpu->cache_count = idx;

	return idx;
}
#endif

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_RISCV)
/*
 *  stress_cpu_cache_get_riscv()
 *	find cache information as provided by the device tree
 */
static int stress_cpu_cache_get_riscv(
	stress_cpu_cache_cpu_t *cpu,
	const char *cpu_path)
{
	typedef struct {
		const char *filename;			/* device-tree filename */
		const stress_cpu_cache_type_t type;	/* cache type */
		const uint16_t level;			/* cache level 1, 2 */
		const cache_size_type_t size_type;	/* cache size field */
		const size_t idx;			/* map to cpu->cache array index */
	} cache_info_t;

	static const cache_info_t cache_info[] = {
		{ "d-cache-block-size",		CACHE_TYPE_DATA,	1, STRESS_CACHE_LINE_SIZE,	0 },
		{ "d-cache-size",		CACHE_TYPE_DATA,	1, STRESS_CACHE_SIZE,		0 },
		{ "i-cache-block-size",		CACHE_TYPE_INSTRUCTION,	1, STRESS_CACHE_LINE_SIZE,	1 },
		{ "i-cache-size",		CACHE_TYPE_INSTRUCTION,	1, STRESS_CACHE_SIZE,		1 },
	};

	char *base;
	const size_t count = 2;
	size_t i;
	bool valid = false;
	int cpu_num;

	/* Parse CPU number */
	base = strrchr(cpu_path, '/');
	if (!base)
		return 0;
	base++;
	if (!*base)
		return 0;
	if (strlen(base) < 4)
		return 0;
	if (sscanf(base + 3, "%d", &cpu_num) != 1)
		return 0;

	cpu->caches = (stress_cpu_cache_t *)calloc(count, sizeof(*(cpu->caches)));
	if (UNLIKELY(!cpu->caches)) {
		pr_err("failed to allocate %zu bytes for cpu caches\n",
			count * sizeof(*(cpu->caches)));
		return 0;
	}

	for (i = 0; i < SIZEOF_ARRAY(cache_info); i++) {
		char path[PATH_MAX];
		const size_t idx = cache_info[i].idx;
		uint32_t value = 0;
		int fd;

		(void)snprintf(path, sizeof(path), "/proc/device-tree/cpus/cpu@%d/%s", cpu_num, cache_info[i].filename);
		fd = open(path, O_RDONLY);
		if (fd >= 0) {
			uint8_t buf[4];

			/* Device tree data is big-endian */
			if (read(fd, buf, sizeof(buf)) == sizeof(buf))
				value = ((uint32_t)buf[0] << 24) |
					((uint32_t)buf[1] << 16) |
					((uint32_t)buf[2] << 8) |
					((uint32_t)buf[3]);
			(void)close(fd);
		}

		cpu->caches[idx].type = cache_info[i].type;
		cpu->caches[idx].level = cache_info[i].level;
		switch (cache_info[i].size_type) {
		case STRESS_CACHE_SIZE:
			cpu->caches[idx].size = value;
			valid = true;
			break;
		case STRESS_CACHE_LINE_SIZE:
			cpu->caches[idx].line_size = (uint32_t)value;
			valid = true;
			break;
		default:
			break;
		}
	}

	if (!valid) {
		free(cpu->caches);
		cpu->caches = NULL;
		cpu->cache_count = 0;

		return 0;
	}
	cpu->cache_count = count;

	return count;
}
#endif

#if defined(__APPLE__)
/*
 *  stress_cpu_cache_get_apple()
 *	find cache information as provided by BSD sysctl
 */
static int stress_cpu_cache_get_apple(stress_cpu_cache_cpu_t *cpu)
{
	typedef struct {
		const char *name;			/* sysctl name */
		const stress_cpu_cache_type_t type;	/* cache type */
		const uint16_t level;			/* cache level 1, 2 */
		const cache_size_type_t size_type;	/* cache size field */
		const size_t idx;			/* map to cpu->cache array index */
	} cache_info_t;

	static const cache_info_t cache_info[] = {
		{ "hw.cachelinesize",		CACHE_TYPE_DATA,	1, STRESS_CACHE_LINE_SIZE,	0 },
		{ "hw.l1dcachesize",		CACHE_TYPE_DATA,	1, STRESS_CACHE_SIZE,		0 },
		{ "hw.cachelinesize",		CACHE_TYPE_INSTRUCTION,	1, STRESS_CACHE_LINE_SIZE,	1 },
		{ "hw.l1icachesize",		CACHE_TYPE_INSTRUCTION,	1, STRESS_CACHE_SIZE,		1 },
		{ "hw.l2cachesize",		CACHE_TYPE_UNIFIED,	2, STRESS_CACHE_SIZE,		2 },
		{ "hw.l3cachesize",		CACHE_TYPE_UNIFIED,	3, STRESS_CACHE_SIZE,		2 },
	};

	const size_t count = 3;
	size_t i;
	bool valid = false;

	cpu->caches = (stress_cpu_cache_t *)calloc(count, sizeof(*(cpu->caches)));
	if (UNLIKELY(!cpu->caches)) {
		pr_err("failed to allocate %zu bytes for cpu caches\n",
			count * sizeof(*(cpu->caches)));
		return 0;
	}

	for (i = 0; i < SIZEOF_ARRAY(cache_info); i++) {
		const size_t idx = cache_info[i].idx;
		uint64_t value;

		value = stress_bsd_getsysctl_uint64(cache_info[i].name);

		cpu->caches[idx].type = cache_info[i].type;
		cpu->caches[idx].level = cache_info[i].level;
		switch (cache_info[i].size_type) {
		case STRESS_CACHE_SIZE:
			cpu->caches[idx].size = value;
			valid = true;
			break;
		case STRESS_CACHE_LINE_SIZE:
			cpu->caches[idx].line_size = (uint32_t)value;
			valid = true;
			break;
		case STRESS_CACHE_WAYS:
			cpu->caches[idx].ways = (uint32_t)value;
			valid = true;
			break;
		default:
			break;
		}
	}

	if (!valid) {
		free(cpu->caches);
		cpu->caches = NULL;
		cpu->cache_count = 0;

		return 0;
	}
	cpu->cache_count = count;

	return count;
}
#endif

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_SPARC)
/*
 *  stress_cpu_cache_get_sparc64()
 *	find cache information as provided by linux SPARC64
 *	/sys/devices/system/cpu/cpu0
 */
static int stress_cpu_cache_get_sparc64(
	stress_cpu_cache_cpu_t *cpu,
	const char *cpu_path)
{
	typedef struct {
		const char *filename;			/* /sys proc name */
		const stress_cpu_cache_type_t type;	/* cache type */
		const uint16_t level;			/* cache level 1, 2 */
		const cache_size_type_t size_type;	/* cache size field */
		const size_t idx;			/* map to cpu->cache array index */
	} cache_info_t;

	static const cache_info_t cache_info[] = {
		{ "l1_dcache_line_size",	CACHE_TYPE_DATA,	1, STRESS_CACHE_LINE_SIZE,	0 },
		{ "l1_dcache_size",		CACHE_TYPE_DATA,	1, STRESS_CACHE_SIZE,		0 },
		{ "l1_icache_line_size",	CACHE_TYPE_INSTRUCTION,	1, STRESS_CACHE_LINE_SIZE,	1 },
		{ "l1_icache_size",		CACHE_TYPE_INSTRUCTION,	1, STRESS_CACHE_SIZE,		1 },
		{ "l2_cache_line_size",		CACHE_TYPE_UNIFIED,	2, STRESS_CACHE_LINE_SIZE,	2 },
		{ "l2_cache_size",		CACHE_TYPE_UNIFIED,	2, STRESS_CACHE_SIZE,		2 },
	};

	const size_t count = 3;
	size_t i;
	bool valid = false;

	cpu->caches = (stress_cpu_cache_t *)calloc(count, sizeof(*(cpu->caches)));
	if (UNLIKELY(!cpu->caches)) {
		pr_err("failed to allocate %zu bytes for cpu caches\n",
			count * sizeof(*(cpu->caches)));
		return 0;
	}

	for (i = 0; i < SIZEOF_ARRAY(cache_info); i++) {
		const size_t idx = cache_info[i].idx;
		uint64_t value;

		if (stress_cpu_cache_get_value(cpu_path, cache_info[i].filename, &value) < 0)
			continue;

		cpu->caches[idx].type = cache_info[i].type;
		cpu->caches[idx].level = cache_info[i].level;
		switch (cache_info[i].size_type) {
		case STRESS_CACHE_SIZE:
			cpu->caches[idx].size = value;
			valid = true;
			break;
		case STRESS_CACHE_LINE_SIZE:
			cpu->caches[idx].line_size = (uint32_t)value;
			valid = true;
			break;
		case STRESS_CACHE_WAYS:
			cpu->caches[idx].ways = (uint32_t)value;
			valid = true;
			break;
		default:
			break;
		}
	}

	if (!valid) {
		free(cpu->caches);
		cpu->caches = NULL;
		cpu->cache_count = 0;

		return 0;
	}
	cpu->cache_count = count;

	return count;
}
#endif

#if defined(STRESS_ARCH_X86)
/*
 *  stress_cpu_cache_get_x86()
 *	find cache information as provided by CPUID. Currently
 *	modern Intel x86 cache info only. Also assumes cpu 0 == cpu n
 *	for cache sizes.
 */
static int stress_cpu_cache_get_x86(stress_cpu_cache_cpu_t *cpu)
{
	uint32_t eax, ebx, ecx, edx;

	if (!stress_cpu_is_x86())
		return 0;

	eax = 0;
	ebx = 0;
	ecx = 0;
	edx = 0;
	stress_asm_x86_cpuid(eax, ebx, ecx, edx);
	if (eax < 0x0b) {
		/* Nehalem-based processors or lower, no cache info */
		return 0;
	}

	eax = 1;
	ebx = 0;
	ecx = 0;
	edx = 0;
	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	/* Currently only handle modern CPUs with cpuid eax = 4 */
	if (edx & (1U << 28)) {
		uint32_t subleaf;
		int i;

		/* Gather max number of cache entries */
		for (i = 0, subleaf = 0; subleaf < 0xff; subleaf++) {
			uint32_t cache_type;

			eax = 4;
			ebx = 0;
			ecx = subleaf;
			edx = 0;
			stress_asm_x86_cpuid(eax, ebx, ecx, edx);
			cache_type = eax & 0x1f;

			if (cache_type == 0)
				 break;
			if (cache_type > 3)
				continue;
			i++;
		}

		/* Now allocate */
		cpu->caches = (stress_cpu_cache_t *)calloc(i, sizeof(*(cpu->caches)));
		if (UNLIKELY(!cpu->caches)) {
			pr_err("failed to allocate %zu bytes for cpu caches\n",
			i * sizeof(*(cpu->caches)));
			return 0;
		}

		/* ..and save */
		for (i = 0, subleaf = 0; subleaf < 0xff; subleaf++) {
			uint32_t cache_type;

			eax = 4;
			ebx = 0;
			ecx = subleaf;
			edx = 0;
			stress_asm_x86_cpuid(eax, ebx, ecx, edx);
			cache_type = eax & 0x1f;

			if (cache_type == 0)
				 break;
			switch (cache_type) {
			case 1:
				cpu->caches[i].type = CACHE_TYPE_DATA;
				break;
			case 2:
				cpu->caches[i].type = CACHE_TYPE_INSTRUCTION;
				break;
			case 3:
				cpu->caches[i].type = CACHE_TYPE_UNIFIED;
				break;
			default:
				continue;
			}

			cpu->caches[i].level = (eax >> 5) & 0x7;
			cpu->caches[i].line_size = ((ebx >> 0) & 0xfff) + 1;
			cpu->caches[i].ways = ((ebx >> 22) & 0x3ff) + 1;
			cpu->caches[i].size = ((uint64_t)(((ebx >> 12) & 0x3ff) + 1) *
					cpu->caches[i].line_size *
					cpu->caches[i].ways *
					(ecx + 1));
			i++;
		}
		cpu->cache_count = i;
		return i;
	}
	return 0;
}
#endif

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_SH4)
static int stress_cpu_cache_get_sh4(stress_cpu_cache_cpu_t *cpu)
{
	FILE *fp;
	char buffer[1024];

	cpu->caches = NULL;
	cpu->cache_count = 0;

	/*
	 * parse the following
	 * icache size	:  4KiB (2-way)
	 * dcache size	:  4KiB (2-way)
	 */

	fp = fopen("/proc/cpuinfo", "r");
	if (UNLIKELY(!fp))
		return 0;

	cpu->caches = (stress_cpu_cache_t *)calloc(2, sizeof(*(cpu->caches)));
	if (UNLIKELY(!cpu->caches)) {
		pr_err("failed to allocate %zu bytes for cpu caches\n",
			2 * sizeof(*(cpu->caches)));
		(void)fclose(fp);
		return 0;
	}

	(void)shim_memset(buffer, 0, sizeof(buffer));
	while ((cpu->cache_count < 2) && fgets(buffer, sizeof(buffer), fp) != NULL) {
		const char *ptr = strchr(buffer, ':');

		if (ptr &&
		    (strncmp("cache size", buffer + 1, 10) == 0) &&
		    ((buffer[0] == 'i') || (buffer[0] == 'd')))   {
			size_t size;

			if (sscanf(ptr + 1, "%zuKiB)", &size) == 1) {
				cpu->caches[cpu->cache_count].type =
					(buffer[0] == 'i') ? CACHE_TYPE_INSTRUCTION : CACHE_TYPE_DATA;
				cpu->caches[cpu->cache_count].size = size * KB;
				cpu->caches[cpu->cache_count].line_size = 64;	/* Assumption! */
				cpu->caches[cpu->cache_count].ways = cpu->caches[cpu->cache_count].size / 64;
				cpu->caches[cpu->cache_count].level = 1;
				cpu->cache_count++;
			}
		}
	}
	(void)fclose(fp);

	return cpu->cache_count;
}
#endif

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_M68K)
static int stress_cpu_cache_get_m68k(stress_cpu_cache_cpu_t *cpu)
{
	FILE *fp;
	char buffer[1024];
	size_t i, count;
	size_t cache_type[2] = { 0, 0 };
	size_t cache_size[2] = { 0, 0 };
	int cpu_id = -1;

	cpu->caches = NULL;
	cpu->cache_count = 0;

	fp = fopen("/proc/cpuinfo", "r");
	if (UNLIKELY(!fp))
		return 0;

	(void)shim_memset(buffer, 0, sizeof(buffer));
	while (fgets(buffer, sizeof(buffer), fp) != NULL) {
		if (strncmp("CPU:", buffer, 4) == 0) {
			if (sscanf(buffer + 4, "%d", &cpu_id) == 1)
				break;
		}
	}
	(void)fclose(fp);

	switch (cpu_id) {
	case 68020:
		count = 1;
		cache_type[0] = CACHE_TYPE_INSTRUCTION;
		cache_size[0] = 256;
		break;
	case 68030:
		count = 2;
		cache_type[0] = CACHE_TYPE_INSTRUCTION;
		cache_size[0] = 256;
		cache_type[1] = CACHE_TYPE_DATA;
		cache_size[1] = 256;
		break;
	case 68040:
		count = 2;
		cache_type[0] = CACHE_TYPE_INSTRUCTION;
		cache_size[0] = 4096;
		cache_type[1] = CACHE_TYPE_DATA;
		cache_size[1] = 4096;
		break;
	case 68060:
		count = 2;
		cache_type[0] = CACHE_TYPE_INSTRUCTION;
		cache_size[0] = 8192;
		cache_type[1] = CACHE_TYPE_DATA;
		cache_size[1] = 8192;
		break;
	default:
		return 0;
	}

	cpu->caches = (stress_cpu_cache_t *)calloc(count, sizeof(*(cpu->caches)));
	if (UNLIKELY(!cpu->caches)) {
		pr_err("failed to allocate %zu bytes for cpu caches\n",
			count * sizeof(*(cpu->caches)));
		return 0;
	}
	for (i = 0; i < count; i++) {
		cpu->caches[i].type = cache_type[i];
		cpu->caches[i].level = 1;
		cpu->caches[i].size = cache_size[i];
		cpu->caches[i].line_size = 64;	/* Assumption! */
		cpu->caches[i].ways = cache_size[i] / 64;
	}
	cpu->cache_count = count;

	return count;
}
#endif

#if defined(__linux__)
/*
 * stress_cpu_cache_size_to_bytes()
 * 	Convert human-readable integer sizes (such as "32K", "4M") into bytes.
 *
 * Supports:
 *
 * - bytes ('B').
 * - kibibytes ('K' - aka KiB).
 * - mebibytes ('M' - aka MiB).
 * - gibibytes ('G' - aka GiB).
 * - tebibutes ('T' - aka TiB).
 *
 * Returns: size in bytes, or 0 on error.
 */
static uint64_t stress_cpu_cache_size_to_bytes(const char *str)
{
	uint64_t bytes;
	int	 ret;
	char	 sz;

	if (UNLIKELY(!str)) {
		pr_dbg("%s: empty string specified\n", __func__);
		return 0;
	}

	ret = sscanf(str, "%" SCNu64 "%c", &bytes, &sz);
	if (ret != 2) {
		pr_dbg("%s: failed to parse suffix from \"%s\"\n",
			__func__, str);
		return 0;
	}

	switch (sz) {
	case 'B':
		/* no-op */
		break;
	case 'K':
		bytes *= KB;
		break;
	case 'M':
		bytes *= MB;
		break;
	case 'G':
		bytes *= GB;
		break;
	case 'T':
		bytes *= TB;
		break;
	default:
		pr_err("unable to convert '%c' size to bytes\n", sz);
		bytes = 0;
		break;
	}
	return bytes;
}
#endif

#if defined(__linux__) ||	\
    defined(__APPLE__)

#if defined(__linux__)

typedef struct {
	const char	*name;			/* cache type name */
	const stress_cpu_cache_type_t value;	/* cache type ID */
} stress_generic_map_t;

static const stress_generic_map_t stress_cpu_cache_type_map[] = {
	{ "data",		CACHE_TYPE_DATA },
	{ "instruction",	CACHE_TYPE_INSTRUCTION },
	{ "unified",		CACHE_TYPE_UNIFIED },
	{  NULL,		CACHE_TYPE_UNKNOWN }
};

/*
 * stress_cpu_cache_get_type()
 * @name: human-readable cache type.
 * Convert a human-readable cache type into a stress_cpu_cache_type_t.
 *
 * Returns: stress_cpu_cache_type_t or CACHE_TYPE_UNKNOWN on error.
 */
static stress_cpu_cache_type_t stress_cpu_cache_get_type(const char *name)
{
	const stress_generic_map_t *p;

	if (UNLIKELY(!name)) {
		pr_dbg("%s: no cache type specified\n", __func__);
		goto out;
	}

	for (p = stress_cpu_cache_type_map; p && p->name; p++) {
		if (!strcasecmp(p->name, name))
			return p->value;
	}

out:
	return CACHE_TYPE_UNKNOWN;
}
#endif

#if defined(__linux__)
/*
 * stress_add_cpu_cache_detail()
 * @cache: stress_cpu_cache_t pointer.
 * @index_path: full /sys path to the particular cpu cache which is to
 *   be represented by @cache.
 * Populate the specified @cache based on the given cache index.
 *
 * Returns: EXIT_FAILURE or EXIT_SUCCESS.
 */
static int stress_add_cpu_cache_detail(stress_cpu_cache_t *cache, const char *index_path)
{
	int ret = EXIT_FAILURE;
	char tmp[2048];
	char path[PATH_MAX];
	uint16_t val16;
	uint32_t val32;

	(void)shim_memset(path, 0, sizeof(path));
	if (UNLIKELY(!cache))
		goto out;
	if (UNLIKELY(!index_path))
		goto out;
	(void)stress_mk_filename(path, sizeof(path), index_path, "type");
	if (stress_get_string_from_file(path, tmp, sizeof(tmp)) < 0)
		goto out;
	cache->type = (stress_cpu_cache_type_t)stress_cpu_cache_get_type(tmp);
	if (cache->type == CACHE_TYPE_UNKNOWN)
		goto out;

	(void)stress_mk_filename(path, sizeof(path), index_path, "size");
	if (stress_get_string_from_file(path, tmp, sizeof(tmp)) < 0)
		goto out;
	cache->size = stress_cpu_cache_size_to_bytes(tmp);

	(void)stress_mk_filename(path, sizeof(path), index_path, "level");
	if (stress_get_string_from_file(path, tmp, sizeof(tmp)) < 0)
		goto out;
	if (sscanf(tmp, "%" SCNu16, &val16) != 1)
		goto out;
	cache->level = val16;
	(void)stress_mk_filename(path, sizeof(path), index_path, "coherency_line_size");
	if (stress_get_string_from_file(path, tmp, sizeof(tmp)) < 0)
		goto out;
	if (sscanf(tmp, "%" SCNu32, &val32) != 1)
		goto out;
	cache->line_size = val32;

	(void)stress_mk_filename(path, sizeof(path), index_path, "ways_of_associativity");
	if (stress_get_string_from_file(path, tmp, sizeof(tmp)) < 0) {
		cache->ways = 0;
	} else {
		if (sscanf(tmp, "%" SCNu32, &cache->ways) != 1)
			cache->ways = 0;
	}
	ret = EXIT_SUCCESS;
out:
	return ret;
}
#endif

#if defined(__linux__)
/*
 *  index_filter()
 *	return 1 when filename is index followed by a digit
 */
static int index_filter(const struct dirent *d)
{
	return ((strncmp(d->d_name, "index", 5) == 0) && isdigit((unsigned char)d->d_name[5]));
}
#endif

#if defined(__linux__)
/*
 *  index_sort()
 *	sort by index number (digits 5 onwards)
 */
static int index_sort(const struct dirent **d1, const struct dirent **d2)
{
	const int i1 = atoi(&(*d1)->d_name[5]);
	const int i2 = atoi(&(*d2)->d_name[5]);

	return i1 - i2;
}
#endif

/*
 *  stress_cpu_cache_get_index()
 *	find cache information as provided by cache info indexes
 *	in /sys/devices/system/cpu/cpu*
 */
static int stress_cpu_cache_get_index(
	stress_cpu_cache_cpu_t *cpu,
	const char *cpu_path)
{
#if defined(__linux__)
	struct dirent **namelist = NULL;
	int n;
	uint32_t i;
	char path[PATH_MAX];

	(void)stress_mk_filename(path, sizeof(path), cpu_path, stress_cpu_cache_dir);
	n = scandir(path, &namelist, index_filter, index_sort);
	if (UNLIKELY(n <= 0)) {
		cpu->caches = NULL;
		return 0;
	}
	cpu->cache_count = (uint32_t)n;
	cpu->caches = (stress_cpu_cache_t *)calloc(cpu->cache_count, sizeof(*(cpu->caches)));
	if (UNLIKELY(!cpu->caches)) {
		size_t cache_bytes = cpu->cache_count * sizeof(*(cpu->caches));

		pr_err("failed to allocate %zu bytes for cpu caches\n",
			cache_bytes);

		cpu->caches = NULL;
		cpu->cache_count = 0;
		goto list_free;
	}

	for (i = 0; i < cpu->cache_count; i++) {
		const char *name = namelist[i]->d_name;
		char fullpath[PATH_MAX];

		(void)shim_memset(fullpath, 0, sizeof(fullpath));
		(void)stress_mk_filename(fullpath, sizeof(fullpath), path, name);
		if (stress_add_cpu_cache_detail(&cpu->caches[i], fullpath) != EXIT_SUCCESS) {
			free(cpu->caches);
			cpu->caches = NULL;
			cpu->cache_count = 0;

			goto list_free;
		}
	}
list_free:
	n = (int)cpu->cache_count;
	stress_dirent_list_free(namelist, n);

	return n;
#else
	(void)cpu;
	(void)cpu_path;
	return 0;
#endif
}

/*
 *  stress_cpu_cache_get_auxval()
 *	find cache information as provided by getauxval
 */
static int stress_cpu_cache_get_auxval(stress_cpu_cache_cpu_t *cpu)
{
#if defined(HAVE_SYS_AUXV_H) && 	\
    defined(HAVE_GETAUXVAL) &&		\
    (defined(AT_L1D_CACHESIZE) ||	\
     defined(AT_L1I_CACHESIZE) ||	\
     defined(AT_L2_CACHESIZE) ||	\
     defined(AT_L3_CACHESIZE))
	typedef struct {
		const unsigned long int auxval_type;
		const stress_cpu_cache_type_t type;	/* cache type */
		const uint16_t level;			/* cache level 1, 2 */
		const cache_size_type_t size_type;	/* cache size field */
		const size_t idx;			/* map to cpu->cache array index */
	} cache_auxval_info_t;

	static const cache_auxval_info_t cache_auxval_info[] = {
#if defined(AT_L1D_CACHESIZE)
		{ AT_L1D_CACHESIZE,	CACHE_TYPE_DATA,	1, STRESS_CACHE_SIZE,	0 },
#endif
#if defined(AT_L1I_CACHESIZE)
		{ AT_L1I_CACHESIZE,	CACHE_TYPE_INSTRUCTION,	1, STRESS_CACHE_SIZE,	1 },
#endif
#if defined(AT_L2_CACHESIZE)
		{ AT_L2_CACHESIZE,	CACHE_TYPE_UNIFIED,	2, STRESS_CACHE_SIZE,	2 },
#endif
#if defined(AT_L3_CACHESIZE)
		{ AT_L3_CACHESIZE,	CACHE_TYPE_UNIFIED,	3, STRESS_CACHE_SIZE,	2 },
#endif
	};

	const size_t count = 4;
	size_t i;
	bool valid = false;

	cpu->caches = (stress_cpu_cache_t *)calloc(count, sizeof(*(cpu->caches)));
	if (UNLIKELY(!cpu->caches)) {
		pr_err("failed to allocate %zu bytes for cpu caches\n",
			count * sizeof(*(cpu->caches)));
		return 0;
	}

	for (i = 0; i < SIZEOF_ARRAY(cache_auxval_info); i++) {
		const uint64_t value = getauxval(cache_auxval_info[i].auxval_type);
		const size_t idx = cache_auxval_info[i].idx;

		if (value)
			valid = true;

		cpu->caches[idx].type = cache_auxval_info[i].type;
		cpu->caches[idx].level = cache_auxval_info[i].level;
		switch (cache_auxval_info[i].size_type) {
		case STRESS_CACHE_SIZE:
			cpu->caches[idx].size = value;
			break;
		case STRESS_CACHE_LINE_SIZE:
			cpu->caches[idx].line_size = (uint32_t)value;
			break;
		case STRESS_CACHE_WAYS:
			cpu->caches[idx].ways = (uint32_t)value;
			break;
		default:
			break;
		}
	}

	if (!valid) {
		free(cpu->caches);
		cpu->caches = NULL;
		cpu->cache_count = 0;

		return 0;
	}

	cpu->cache_count = count;

	return count;
#else
	(void)cpu;

	return 0;
#endif
}

/*
 * stress_cpu_cache_get_details()
 * @cpu: cpu to fill in.
 * @cpu_path: Full /sys path to cpu which will be represented by @cpu.
 * Populate @cpu with details from @cpu_path.
 *
 * Returns: EXIT_FAILURE or EXIT_SUCCESS.
 */
static void stress_cpu_cache_get_details(stress_cpu_cache_cpu_t *cpu, const char *cpu_path)
{
	if (UNLIKELY(!cpu)) {
		pr_dbg("%s: invalid cpu parameter\n", __func__);
		return;
	}
	if (UNLIKELY(!cpu_path)) {
		pr_dbg("%s: invalid cpu path parameter\n", __func__);
		return;
	}

	/* The default x86 cache method */
	if (stress_cpu_cache_get_index(cpu, cpu_path) > 0)
		return;

	/* Try cache info using auxinfo */
	if (stress_cpu_cache_get_auxval(cpu) > 0)
		return;

#if defined(STRESS_ARCH_X86)
	/* Try CPUID info */
	if (stress_cpu_cache_get_x86(cpu) > 0)
		return;
#endif

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_SPARC)
	/* Try cache info for sparc CPUs */
	if (stress_cpu_cache_get_sparc64(cpu, cpu_path) > 0)
		return;
#endif

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_M68K)
	if (stress_cpu_cache_get_m68k(cpu) > 0)
		return;
#endif

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_SH4)
	if (stress_cpu_cache_get_sh4(cpu) > 0)
		return;
#endif

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_ALPHA)
	if (stress_cpu_cache_get_alpha(cpu, cpu_path) > 0)
		return;
#endif

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_RISCV)
	if (stress_cpu_cache_get_riscv(cpu, cpu_path))
		return;
#endif

#if defined(__APPLE__)
	if (stress_cpu_cache_get_apple(cpu) > 0)
		return;
#endif

	return;
}
#endif

#if defined(__linux__)

/*
 *  stress_cpu_cache_filter()
 *	return 1 when filename is cpu followed by a digit
 */
static int stress_cpu_cache_filter(const struct dirent *d)
{
	return ((strncmp(d->d_name, "cpu", 3) == 0) && isdigit((unsigned char)d->d_name[3]));
}

/*
 *  cpu_sort()
 *	sort by CPU number (digits 3 onwards)
 */
static int cpu_sort(const struct dirent **d1, const struct dirent **d2)
{
	const int c1 = atoi(&(*d1)->d_name[3]);
	const int c2 = atoi(&(*d2)->d_name[3]);

	return c1 - c2;
}

/*
 * stress_cpu_cache_get_all_details()
 * Obtain information on all cpus caches on the system.
 *
 * Returns: dynamically-allocated stress_cpu_cache_cpus_t object, or NULL on error.
 */
stress_cpu_cache_cpus_t *stress_cpu_cache_get_all_details(void)
{
	int i, cpu_count;
	stress_cpu_cache_cpus_t *cpus = NULL;
	struct dirent **namelist = NULL;

	cpu_count = scandir(stress_sys_cpu_prefix, &namelist, stress_cpu_cache_filter, cpu_sort);
	if (UNLIKELY(cpu_count < 1)) {
		pr_err("no CPUs found in %s\n", stress_sys_cpu_prefix);
		goto out;
	}
	cpus = (stress_cpu_cache_cpus_t *)calloc(1, sizeof(*cpus));
	if (UNLIKELY(!cpus))
		goto out;

	cpus->cpus = (stress_cpu_cache_cpu_t *)calloc((size_t)cpu_count, sizeof(*(cpus->cpus)));
	if (UNLIKELY(!cpus->cpus)) {
		free(cpus);
		cpus = NULL;
		goto out;
	}
	cpus->count = (uint32_t)cpu_count;

	for (i = 0; i < cpu_count; i++) {
		const char *name = namelist[i]->d_name;
		char fullpath[PATH_MAX];
		stress_cpu_cache_cpu_t *const cpu = &cpus->cpus[i];

		(void)shim_memset(fullpath, 0, sizeof(fullpath));
		(void)stress_mk_filename(fullpath, sizeof(fullpath), stress_sys_cpu_prefix, name);
		cpu->num = (uint32_t)i;
		if (cpu->num == 0) {
			/* 1st CPU cannot be taken offline */
			cpu->online = true;
		} else {
			char onlinepath[PATH_MAX + 8];
			char tmp[2048];

			(void)shim_memset(onlinepath, 0, sizeof(onlinepath));
			(void)snprintf(onlinepath, sizeof(onlinepath), "%s/%s/online", stress_sys_cpu_prefix, name);
			if (stress_get_string_from_file(onlinepath, tmp, sizeof(tmp)) < 0) {
				/* Assume it is online, it is the best we can do */
				cpu->online = true;
			} else {
				int online;

				if (sscanf(tmp, "%d", &online) == 1)
					cpu->online = (bool)online;
			}
		}
		if (cpu->online)
			stress_cpu_cache_get_details(&cpus->cpus[i], fullpath);
	}

out:
	stress_dirent_list_free(namelist, cpu_count);
	return cpus;
}
#elif defined(__APPLE__)
/*
 * stress_cpu_cache_get_all_details()
 * Obtain information on all cpus caches on the system.
 *
 * Returns: dynamically-allocated stress_cpu_cache_cpus_t object, or NULL on error.
 */
stress_cpu_cache_cpus_t *stress_cpu_cache_get_all_details(void)
{
	int32_t i, cpu_count;
	stress_cpu_cache_cpus_t *cpus = NULL;
	struct dirent **namelist = NULL;

	if (stress_bsd_getsysctl("hw.physicalcpu", &cpu_count, sizeof(cpu_count)) < 0) {
		pr_err("no CPUs found using sysctl hw.physicalcpu\n");
		goto out;
	}
	cpus = (stress_cpu_cache_cpus_t *)calloc(1, sizeof(*cpus));
	if (UNLIKELY(!cpus))
		goto out;

	cpus->cpus = (stress_cpu_cache_cpu_t *)calloc((size_t)cpu_count, sizeof(*(cpus->cpus)));
	if (UNLIKELY(!cpus->cpus)) {
		free(cpus);
		cpus = NULL;
		goto out;
	}
	cpus->count = (uint32_t)cpu_count;

	for (i = 0; i < cpu_count; i++) {
		stress_cpu_cache_get_details(&cpus->cpus[i], "");
	}

out:
	stress_dirent_list_free(namelist, cpu_count);
	return cpus;
}
#elif defined(STRESS_ARCH_X86)
stress_cpu_cache_cpus_t *stress_cpu_cache_get_all_details(void)
{
	uint32_t eax, ebx, ecx, edx;
	int32_t i, cpu_count;
	stress_cpu_cache_cpus_t *cpus;

	if (!stress_cpu_is_x86())
		return NULL;

	cpu_count = stress_get_processors_configured();

	eax = 0;
	ebx = 0;
	ecx = 0;
	edx = 0;
	stress_asm_x86_cpuid(eax, ebx, ecx, edx);
	if (eax < 0x0b) {
		/* Nehalem-based processors or lower, no cache info */
		return NULL;
	}
	cpus = (stress_cpu_cache_cpus_t *)calloc(1, sizeof(*cpus));
	if (UNLIKELY(!cpus))
		return NULL;
	cpus->cpus = (stress_cpu_cache_cpu_t *)calloc((size_t)cpu_count, sizeof(*(cpus->cpus)));
	if (UNLIKELY(!cpus->cpus)) {
		free(cpus);
		return NULL;
	}
	cpus->count = (uint32_t)cpu_count;

	for (i = 0; i < cpu_count; i++) {
		stress_cpu_cache_get_x86(&cpus->cpus[i]);
	}
	return cpus;
}
#else
stress_cpu_cache_cpus_t *stress_cpu_cache_get_all_details(void)
{
	return NULL;
}
#endif

/*
 * stress_free_cpu_caches()
 * @cpus: value returned by get_all_cpu_cache_details().
 *
 * Undo the action of get_all_cpu_cache_details() by freeing all
 * associated resources.
 */
void stress_free_cpu_caches(stress_cpu_cache_cpus_t *cpus)
{
	uint32_t  i;

	if (!cpus)
		return;

	for (i = 0; i < cpus->count; i++) {
		stress_cpu_cache_cpu_t *cpu = &cpus->cpus[i];

		if (cpu->caches) {
			free(cpu->caches);
			cpu->caches = NULL;
		}
	}
	free(cpus->cpus);
	cpus->cpus = NULL;
	free(cpus);
}

/*
 *  stress_cpu_cache_get_llc_size()
 * 	get Lower Level Cache size and Cache Line size (sizes in bytes)
 *	sizes are zero if not available.
 */
void stress_cpu_cache_get_llc_size(size_t *llc_size, size_t *cache_line_size)
{
#if defined(__linux__) ||	\
    defined(__APPLE__) ||	\
    defined(STRESS_ARCH_X86)
	uint16_t max_cache_level;
	stress_cpu_cache_cpus_t *cpu_caches;
	stress_cpu_cache_t *cache = NULL;

	*llc_size = 0;
	*cache_line_size = 0;

	cpu_caches = stress_cpu_cache_get_all_details();
	if (UNLIKELY(!cpu_caches))
		return;

	max_cache_level = stress_cpu_cache_get_max_level(cpu_caches);
	if (UNLIKELY(max_cache_level < 1))
		goto free_cpu_caches;
	cache = stress_cpu_cache_get(cpu_caches, max_cache_level);
	if (UNLIKELY(!cache))
		goto free_cpu_caches;

	*llc_size = cache->size;
	*cache_line_size = cache->line_size ? cache->line_size : 64;

free_cpu_caches:
	stress_free_cpu_caches(cpu_caches);
#else
	*llc_size = 0;
	*cache_line_size = 0;
#endif
}

/*
 *  stress_cpu_cache_get_level_size()
 *	get cpu cache size for a specific cache level
 */
void stress_cpu_cache_get_level_size(const uint16_t cache_level, size_t *cache_size, size_t *cache_line_size)
{
#if defined(__linux__) ||	\
    defined(__APPLE__) ||	\
    defined(STRESS_ARCH_X86)
	stress_cpu_cache_cpus_t *cpu_caches;
	stress_cpu_cache_t *cache = NULL;

	*cache_size = 0;
	*cache_line_size = 0;

	cpu_caches = stress_cpu_cache_get_all_details();
	if (UNLIKELY(!cpu_caches))
		return;

	cache = stress_cpu_cache_get(cpu_caches, cache_level);
	if (UNLIKELY(!cache))
		goto free_cpu_caches;

	*cache_size = cache->size;
	*cache_line_size = cache->line_size ? cache->line_size : 64;

free_cpu_caches:
	stress_free_cpu_caches(cpu_caches);
#else
	*cache_size = 0;
	*cache_line_size = 0;
#endif
}

/*
 *  stress_cpu_data_cache_flush()
 *	flush data cache, optimal down to more generic
 */
void OPTIMIZE3 stress_cpu_data_cache_flush(void *addr, const size_t len)
{
#if defined(HAVE_ASM_X86_CLFLUSHOPT) ||	\
    defined(HAVE_ASM_X86_CLFLUSH) ||	\
    defined(HAVE_BUILTIN___CLEAR_CACHE)
	register uint8_t *ptr = (uint8_t *)addr;
	register uint8_t *ptr_end = ptr + len;
#endif

#if defined(HAVE_ASM_X86_CLFLUSHOPT)
	if (stress_cpu_x86_has_clflushopt()) {
		while (ptr < ptr_end) {
			stress_asm_x86_clflushopt((void *)ptr);
			ptr += 64;
		}
		return;
	}
#endif
#if defined(HAVE_ASM_X86_CLFLUSH)
	if (stress_cpu_x86_has_clfsh()) {
		while (ptr < ptr_end) {
			stress_asm_x86_clflush((void *)ptr);
			ptr += 64;
		}
		return;
	}
#endif
#if defined(HAVE_BUILTIN___CLEAR_CACHE)
	__builtin___clear_cache(addr, (void *)ptr_end);
#else
	shim_cacheflush(addr, len, SHIM_DCACHE);
#endif
}
