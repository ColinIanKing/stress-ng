/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include <signal.h>
#include <time.h>
#include <aio.h>
#include <mqueue.h>
#include <sys/mman.h>

/* The following functions from librt are used by stress-ng */

static void *rt_funcs[] = {
#if defined(__linux__)
	(void *)timer_create,
	(void *)timer_settime,
	(void *)timer_gettime,
	(void *)timer_getoverrun,
	(void *)timer_delete,
#endif
#if defined(__linux__) &&               \
    defined(__NR_io_setup) &&           \
    defined(__NR_io_destroy) &&         \
    defined(__NR_io_submit) &&          \
    defined(__NR_io_getevents)
	(void *)aio_write,
	(void *)aio_error,
	(void *)aio_cancel,
	(void *)aio_read,
	(void *)aio_write,
#endif
#if defined(__linux__)
	(void *)mq_open,
	(void *)mq_send,
	(void *)mq_send,
	(void *)mq_close,
	(void *)mq_unlink,
	(void *)mq_receive,
	(void *)mq_timedreceive,
	(void *)mq_getattr,
#endif
	(void *)shm_open,
	(void *)shm_unlink,
	(void *)shm_unlink,
};

int main(void)
{
	return 0;
}
