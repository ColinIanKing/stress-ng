// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <unistd.h>
#include <sys/inotify.h>

int main(void)
{
    int fd;

    fd = inotify_init1(0);
    if (fd < 0) {
        return -1;
    }

    (void)close(fd);
    return 0;
}
