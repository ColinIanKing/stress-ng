// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <signal.h>
#include <time.h>
#include <aio.h>
#include <mqueue.h>
#include <sys/mman.h>

/* The following functions from librt are used by stress-ng */

static void *rt_funcs[] = {
#if defined(__linux__) &&               \
    defined(__NR_io_setup) &&           \
    defined(__NR_io_destroy) &&         \
    defined(__NR_io_submit) &&          \
    defined(__NR_io_getevents)
	(void *)aio_cancel,
	(void *)aio_error,
	(void *)aio_init,
	(void *)aio_read,
	(void *)aio_write,
#endif
#if defined(__linux__)
	(void *)mq_close,
	(void *)mq_getattr,
	(void *)mq_open,
	(void *)mq_receive,
	(void *)mq_send,
	(void *)mq_timedreceive,
	(void *)mq_unlink,
#endif
	(void *)shm_open,
	(void *)shm_unlink,
#if defined(__linux__)
	(void *)timer_create,
	(void *)timer_delete,
	(void *)timer_gettime,
	(void *)timer_getoverrun,
	(void *)timer_settime,
#endif
};

int main(void)
{
	return 0;
}
