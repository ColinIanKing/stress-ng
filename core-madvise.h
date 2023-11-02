/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_MADVISE_H
#define CORE_MADVISE_H

extern int stress_madvise_random(void *addr, const size_t length);
extern void stress_madvise_pid_all_pages(const pid_t pid, const int advise);

#endif
