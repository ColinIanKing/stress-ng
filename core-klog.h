/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_KLOG_H
#define CORE_KLOG_H

extern void stress_klog_start(void);
extern void stress_klog_stop(bool *success);

#endif
