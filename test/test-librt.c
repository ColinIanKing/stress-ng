/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King
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
#include <stdio.h>
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
	size_t i;

	for (i = 0; i < sizeof(rt_funcs) / sizeof(rt_funcs[0]); i++)
		printf("%p\n", rt_funcs[i]);

	return 0;
}
