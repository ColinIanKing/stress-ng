/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#ifndef CORE_IO_PRIORITY_H
#define CORE_IO_PRIORITY_H

/*
 *  See ioprio_set(2) and linux/ioprio.h, glibc has no definitions
 *  for these at present. Also refer to Documentation/block/ioprio.txt
 *  in the Linux kernel source.
 */
#if !defined(IOPRIO_CLASS_RT)
#define IOPRIO_CLASS_RT         (1)
#endif
#if !defined(IOPRIO_CLASS_BE)
#define IOPRIO_CLASS_BE         (2)
#endif
#if !defined(IOPRIO_CLASS_IDLE)
#define IOPRIO_CLASS_IDLE       (3)
#endif

#if !defined(IOPRIO_WHO_PROCESS)
#define IOPRIO_WHO_PROCESS      (1)
#endif
#if !defined(IOPRIO_WHO_PGRP)
#define IOPRIO_WHO_PGRP         (2)
#endif
#if !defined(IOPRIO_WHO_USER)
#define IOPRIO_WHO_USER         (3)
#endif

#if !defined(IOPRIO_PRIO_VALUE)
#define IOPRIO_PRIO_VALUE(class, data)  (((class) << 13) | data)
#endif

#endif
