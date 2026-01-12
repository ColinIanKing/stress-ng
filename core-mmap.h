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

extern void stress_mmap_set(uint8_t *buf, const size_t sz, const size_t page_size);
extern int stress_mmap_check(uint8_t *buf, const size_t sz, const size_t page_size);
extern void stress_mmap_set_light(uint8_t *buf, const size_t sz, const size_t page_size);
extern int stress_mmap_check_light(uint8_t *buf, const size_t sz, const size_t page_size);
extern WARN_UNUSED void *stress_mmap_populate(void *addr, size_t length, int prot,
	int flags, int fd, off_t offset);
extern WARN_UNUSED void *stress_mmap_anon_shared(size_t length, int prot);
extern int stress_munmap_anon_shared(void *addr, size_t length);
extern WARN_UNUSED int stress_mmap_stats(void *addr, const size_t length, stress_mmap_stats_t *stats);
extern void stress_mmap_stats_sum(stress_mmap_stats_t *stats_total, const stress_mmap_stats_t *stats);
extern void stress_mmap_stats_report(stress_args_t *args, const stress_mmap_stats_t *stats,
	int *metric_index, const int flags);
extern void stress_mmap_populate_forward(void *addr, const size_t len, const int prot);
extern void stress_mmap_populate_reverse(void *addr, const size_t len, const int prot);

#endif
