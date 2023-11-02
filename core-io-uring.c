// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "config.h"

#if defined(__linux__) &&	\
    defined(HAVE_LINUX_IO_URING_H)
#include <linux/io_uring.h>
#endif
