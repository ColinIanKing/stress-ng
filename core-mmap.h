/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_MMAP_H
#define CORE_MMAP_H

extern void stress_mmap_set(uint8_t *buf, const size_t sz, const size_t page_size);
extern int stress_mmap_check( uint8_t *buf, const size_t sz, const size_t page_size);

#endif
