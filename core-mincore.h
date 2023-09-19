/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_MINCORE_H
#define CORE_MINCORE_H

extern int stress_mincore_touch_pages(void *buf, const size_t buf_len);
extern int stress_mincore_touch_pages_interruptible(void *buf, const size_t buf_len);

#endif
