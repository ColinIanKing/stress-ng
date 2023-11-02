// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "config.h"

#if defined(__linux__) &&	\
    defined(HAVE_SYS_PERSONALITY_H)
#include <sys/personality.h>
#endif
