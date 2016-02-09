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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_ZLIB)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include "zlib.h"

#define DATA_SIZE 	(65536)		/* Must be a multiple of 8 bytes */

typedef void (*stress_rand_data_func)(uint32_t *data, const int size);

/*
 *  stress_rand_data_binary()
 *	fill buffer with random binary data
 */
static void stress_rand_data_binary(uint32_t *data, const int size)
{
	const int n = size / sizeof(uint32_t);
	register int i;

	for (i = 0; i < n; i++, data++)
		*data = mwc32();
}

/*
 *  stress_rand_data_text()
 *	fill buffer with random ASCII text
 */
static void stress_rand_data_text(uint32_t *data, const int size)
{
	stress_strnrnd((char *)data, size);
}

/*
 *  stress_rand_data_01()
 *	fill buffer with random ASCII 0 or 1
 */
static void stress_rand_data_01(uint32_t *data, const int size)
{
	unsigned char *ptr = (unsigned char *)data;
	register int i;

	for (i = 0; i < size; i += 8, ptr += 8) {
		uint8_t v = mwc8();

		*(ptr + 0) = '0' + (v & 1);
		v >>= 1;
		*(ptr + 1) = '0' + (v & 1);
		v >>= 1;
		*(ptr + 2) = '0' + (v & 1);
		v >>= 1;
		*(ptr + 3) = '0' + (v & 1);
		v >>= 1;
		*(ptr + 4) = '0' + (v & 1);
		v >>= 1;
		*(ptr + 5) = '0' + (v & 1);
		v >>= 1;
		*(ptr + 6) = '0' + (v & 1);
		v >>= 1;
		*(ptr + 7) = '0' + (v & 1);
	}
}

/*
 *  stress_rand_data_digits()
 *	fill buffer with random ASCII '0' .. '9'
 */
static void stress_rand_data_digits(uint32_t *data, const int size)
{
	unsigned char *ptr = (unsigned char *)data;
	register int i;

	for (i = 0; i < size; i++, ptr++)
		*ptr = '0' + (mwc32() % 10);
}

/*
 *  stress_rand_data_00_ff()
 *	fill buffer with random 0x00 or 0xff
 */
static void stress_rand_data_00_ff(uint32_t *data, const int size)
{
	unsigned char *ptr = (unsigned char *)data;
	register int i;

	for (i = 0; i < size; i += 8, ptr += 8) {
		uint8_t v = mwc8();

		*(ptr + 0) = (v & 1) ? 0x00 : 0xff;
		*(ptr + 1) = (v & 2) ? 0x00 : 0xff;
		*(ptr + 2) = (v & 4) ? 0x00 : 0xff;
		*(ptr + 3) = (v & 8) ? 0x00 : 0xff;
		*(ptr + 4) = (v & 16) ? 0x00 : 0xff;
		*(ptr + 5) = (v & 32) ? 0x00 : 0xff;
		*(ptr + 6) = (v & 64) ? 0x00 : 0xff;
		*(ptr + 7) = (v & 128) ? 0x00 : 0xff;
	}
}

/*
 *  stress_rand_data_nybble()
 *	fill buffer with 0x00..0x0f
 */
static void stress_rand_data_nybble(uint32_t *data, const int size)
{
	unsigned char *ptr = (unsigned char *)data;
	register int i;

	for (i = 0; i < size; i += 8, ptr += 8) {
		uint32_t v = mwc32();

		*(ptr + 0) = v & 0xf;
		v >>= 4;
		*(ptr + 1) = v & 0xf;
		v >>= 4;
		*(ptr + 2) = v & 0xf;
		v >>= 4;
		*(ptr + 3) = v & 0xf;
		v >>= 4;
		*(ptr + 4) = v & 0xf;
		v >>= 4;
		*(ptr + 5) = v & 0xf;
		v >>= 4;
		*(ptr + 6) = v & 0xf;
		v >>= 4;
		*(ptr + 7) = v & 0xf;
	}
}

/*
 *  stress_rand_data_rarely_1()
 *	fill buffer with data that is 1 in every 32 bits 1
 */
static void stress_rand_data_rarely_1(uint32_t *data, const int size)
{
	const int n = size / sizeof(uint32_t);
	register int i;

	for (i = 0; i < n; i++, data++)
		*data = 1 << (mwc32() & 0x1f);
}

/*
 *  stress_rand_data_rarely_0()
 *	fill buffer with data that is 1 in every 32 bits 0
 */
static void stress_rand_data_rarely_0(uint32_t *data, const int size)
{
	const int n = size / sizeof(uint32_t);
	register int i;

	for (i = 0; i < n; i++, data++)
		*data = ~(1 << (mwc32() & 0x1f));
}

static const stress_rand_data_func rand_data_funcs[] = {
	stress_rand_data_rarely_1,
	stress_rand_data_rarely_0,
	stress_rand_data_binary,
	stress_rand_data_text,
	stress_rand_data_01,
	stress_rand_data_digits,
	stress_rand_data_00_ff,
	stress_rand_data_nybble
};

/*
 *  stress_zlib_err()
 *	turn a zlib error to something human readable
 */
const char *stress_zlib_err(const int zlib_err)
{
	static char buf[1024];

	switch (zlib_err) {
	case Z_OK:
		return "no error";
	case Z_ERRNO:
		snprintf(buf, sizeof(buf), "system error, errno=%d (%s)\n",
			errno, strerror(errno));
		return buf;
	case Z_STREAM_ERROR:
		return "invalid compression level";
	case Z_DATA_ERROR:
		return "invalid or incomplete deflate data";
	case Z_MEM_ERROR:
		return "out of memory";
	case Z_VERSION_ERROR:
		return "zlib version mismatch";
	default:
		snprintf(buf, sizeof(buf), "unknown zlib error %d\n", zlib_err);
		return buf;
	}
}


/*
 *  stress_zlib_inflate()
 *	inflate compressed data out of the read
 *	end of a pipe fd
 */
int stress_zlib_inflate(const char *name, const int fd)
{
	int ret;
	z_stream stream_inf;

	stream_inf.zalloc = Z_NULL;
	stream_inf.zfree = Z_NULL;
	stream_inf.opaque = Z_NULL;

	ret = inflateInit(&stream_inf);
	if (ret != Z_OK) {
		pr_fail(stderr, "%s: zlib inflateInit error: %s\n",
			name, stress_zlib_err(ret));
		return EXIT_FAILURE;
	}

	for (;;) {
		ssize_t sz;
		unsigned char in[DATA_SIZE];

		sz = read(fd, in, DATA_SIZE);
		if (sz <= 0)
			break;
		stream_inf.avail_in = sz;
		stream_inf.next_in = in;

		do {
			unsigned char out[DATA_SIZE];

			stream_inf.avail_out = DATA_SIZE;
			stream_inf.next_out = out;

			ret = inflate(&stream_inf, Z_NO_FLUSH);
		} while ((ret == Z_OK) && (stream_inf.avail_out == 0));
	}
	(void)inflateEnd(&stream_inf);

	return ((ret == Z_OK) || (ret == Z_STREAM_END)) ?
		EXIT_SUCCESS : EXIT_FAILURE;
}

/*
 *  stress_zlib_deflate()
 *	compress random data and write it down the
 *	write end of a pipe fd
 */
int stress_zlib_deflate(
	const char *name,
	const int fd,
	const uint32_t instance,
	const uint64_t max_ops,
	uint64_t *counter)
{
	int ret;
	bool do_run;
	z_stream stream_def;
	uint64_t bytes_in = 0, bytes_out = 0;

	stream_def.zalloc = Z_NULL;
	stream_def.zfree = Z_NULL;
	stream_def.opaque = Z_NULL;

	ret = deflateInit(&stream_def, Z_BEST_COMPRESSION);
	if (ret != Z_OK) {
		pr_fail(stderr, "%s: zlib deflateInit error: %s\n",
			name, stress_zlib_err(ret));
		return EXIT_FAILURE;
	}

	do {
		uint32_t in[DATA_SIZE / sizeof(uint32_t)];

		rand_data_funcs[mwc32() % SIZEOF_ARRAY(rand_data_funcs)](in, DATA_SIZE);
		stream_def.avail_in = DATA_SIZE;
		stream_def.next_in = (unsigned char *)in;

		do_run = opt_do_run && (!max_ops || *counter < max_ops);

		bytes_in += DATA_SIZE;

		do {
			unsigned char out[DATA_SIZE];
			int def_size, rc;
			int flush = do_run ? Z_NO_FLUSH : Z_FINISH;

			stream_def.avail_out = DATA_SIZE;
			stream_def.next_out = out;
			rc = deflate(&stream_def, flush);

			if ((rc != Z_OK) && (rc != Z_STREAM_END)) {
				pr_fail(stderr, "%s: zlib deflate error: %s\n",
					name, stress_zlib_err(rc));
				do_run = false;
				ret = EXIT_FAILURE;
				break;
			}
			def_size = DATA_SIZE - stream_def.avail_out;
			bytes_out += def_size;
			if (write(fd, out, def_size) != def_size) {
				if ((errno != EINTR) && (errno != EPIPE)) {
					ret = EXIT_FAILURE;
					pr_fail(stderr, "%s: write error: errno=%d (%s)\n",
						name, errno, strerror(errno));
				}
				do_run = false;
				break;
			}
			(*counter)++;
		} while (do_run && stream_def.avail_out == 0);
	} while (do_run);

	pr_inf(stderr, "%s: instance %" PRIu32 ": compression ratio: %5.2f%%\n",
		name, instance, 100.0 * (double)bytes_out / (double)bytes_in);

	(void)deflateEnd(&stream_def);
	return ret;
}

/*
 *  stress_zlib()
 *	stress cpu with compression and decompression
 */
int stress_zlib(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int ret, fds[2], status;
	pid_t pid;

	if (pipe(fds) < 0) {
		pr_err(stderr, "%s: pipe failed, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	pid = fork();
	if (pid < 0) {
		(void)close(fds[0]);
		(void)close(fds[1]);
		pr_err(stderr, "%s: fork failed, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		setpgid(0, pgrp);
		stress_parent_died_alarm();

		(void)close(fds[1]);
		ret = stress_zlib_inflate(name, fds[0]);
		(void)close(fds[0]);

		exit(ret);
	} else {
		(void)close(fds[0]);
		ret = stress_zlib_deflate(name, fds[1], instance, max_ops, counter);
		(void)close(fds[1]);
	}
	(void)kill(pid, SIGKILL);
	(void)waitpid(pid, &status, 0);

	return ret;
}

#endif
