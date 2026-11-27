/*
 * Copyright (C) 2024-2026 Colin Ian King.
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
#ifndef CORE_MMAP_H
#define CORE_MMAP_H

#define STRESS_MMAP_REPORT_FLAGS_TOTAL		(0x0001)
#define STRESS_MMAP_REPORT_FLAGS_PRESENT	(0x0002)
#define STRESS_MMAP_REPORT_FLAGS_SWAPPED	(0x0004)
#define STRESS_MMAP_REPORT_FLAGS_DIRTIED	(0x0008)
#define STRESS_MMAP_REPORT_FLAGS_EXCLUSIVE	(0x0010)
#define STRESS_MMAP_REPORT_FLAGS_UKNOWN		(0x0020)
#define STRESS_MMAP_REPORT_FLAGS_NULL		(0x0040)
#define STRESS_MMAP_REPORT_FLAGS_CONTIGUOUS	(0x0080)

/*
 *  stress_mmap_stats_t used for page stats used
 *  by stress_mmap_stats_* helpers
 */
typedef struct {
	size_t pages_mapped;		/* number of pages mmap'd */
	size_t pages_present;		/* number of pages present in memory */
	size_t pages_swapped;		/* number of pages swapped out */
	size_t pages_contiguous;	/* number of physical contiguous pages */
	size_t pages_dirtied;		/* number of soft dirty pages */
	size_t pages_exclusive;		/* number of pages exclusively mapped */
	size_t pages_unknown;		/* number of pages with unknown map state */
	size_t pages_null;		/* number of pages with physical zero address */
} stress_mmap_stats_t;

#define STRESS_MMAP_READ	(0x01)
#define STRESS_MMAP_WRITE	(0x02)
#define STRESS_MMAP_EXEC	(0x04)
#define STRESS_MMAP_PRIVATE	(0x08)
#define STRESS_MMAP_SHARED	(0x10)
#define STRESS_MMAP_ANON	(0x20)

typedef struct {
	uintptr_t mmap_begin;	/* mapping beginning */
	uintptr_t mmap_end;	/* mapping ending */
	uintptr_t mmap_offset;	/* mapping offset into mmap'd file */
	uintptr_t *addrs;	/* address of pages, can be NULL */
	size_t	  size;		/* mapping size in bytes */
	size_t	  n_pages;	/* mapping size in pages */
	int prot;		/* protection STRESS_MMAP_PROT_* */
	int flags;		/* stressor specific flags */
	char *path;		/* mapping file, NULL if none */
} stress_mmap_t;

typedef struct stress_mmaps {
        stress_mmap_t  *mmaps;	/* array of mmaps */
        size_t n_mmaps;		/* number of mmaps */
} stress_mmaps_t;

extern void stress_mmap_set(uint8_t *buf, const size_t sz, const size_t page_size);
extern int stress_mmap_check(uint8_t *buf, const size_t sz, const size_t page_size);
extern void stress_mmap_set_light(uint8_t *buf, const size_t sz, const size_t page_size);
extern int stress_mmap_check_light(uint8_t *buf, const size_t sz, const size_t page_size);
extern WARN_UNUSED void *stress_mmap_populate(void *addr, size_t length, int prot,
	int flags, int fd, off_t offset);
extern WARN_UNUSED void *stress_mmap_anon_shared(const size_t length, const int prot);
extern int stress_munmap_anon_shared(void *addr, const size_t length);
extern int stress_munmap_force(void *addr, const size_t length);
extern WARN_UNUSED int stress_mmap_stats(void *addr, const size_t length, stress_mmap_stats_t *stats);
extern void stress_mmap_stats_sum(stress_mmap_stats_t *stats_total, const stress_mmap_stats_t *stats);
extern void stress_mmap_stats_report(stress_args_t *args, const stress_mmap_stats_t *stats,
	const int flags);
extern void stress_mmap_discontiguous(void *addr, const size_t len);
extern void stress_mmap_populate_forward(void *addr, const size_t len, const int prot);
extern void stress_mmap_populate_reverse(void *addr, const size_t len, const int prot);

extern int stress_mmaps_get(const pid_t pid, const size_t page_size,
	const int prot_mask, const bool alloc_addrs, const bool alloc_paths,
	stress_mmaps_t *mmaps);
extern void stress_mmaps_free(stress_mmaps_t *mmaps);

static inline void stress_mmap_stats_clear(stress_mmap_stats_t *stats)
{
	(void)memset(stats, 0, sizeof(*stats));
}

#endif
