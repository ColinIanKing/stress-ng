/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#ifndef CORE_MADVISE_H
#define CORE_MADVISE_H

extern const int madvise_options[];
extern const size_t madvise_options_elements;

extern int stress_madvise_randomize(void *addr, const size_t length);
extern int stress_madvise_random(void *addr, const size_t length);
extern int stress_madvise_mergeable(void *addr, const size_t length);
extern int stress_madvise_collapse(void *addr, size_t length);
extern int stress_madvise_willneed(void *addr, const size_t length);
extern int stress_madvise_nohugepage(void *addr, const size_t length);
extern void stress_madvise_pid_all_pages(const pid_t pid, const int *advice, const size_t n_advice);

#endif
