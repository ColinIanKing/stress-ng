/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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
#include "stress-ng.h"

#if defined(HAVE_LIB_Z)

#include "zlib.h"

#define DATA_SIZE_1K 	(KB)		/* Must be a multiple of 8 bytes */
#define DATA_SIZE_4K 	(KB * 4)	/* Must be a multiple of 8 bytes */
#define DATA_SIZE_16K 	(KB * 16)	/* Must be a multiple of 8 bytes */
#define DATA_SIZE_64K 	(KB * 64)	/* Must be a multiple of 8 bytes */
#define DATA_SIZE_128K 	(KB * 128)	/* Must be a multiple of 8 bytes */

#define DATA_SIZE DATA_SIZE_64K

typedef void (*stress_zlib_rand_data_func)(uint32_t *data, const int size);

typedef struct {
	const char *name;			/* human readable form of random data generation selection */
	const stress_zlib_rand_data_func func;	/* the random data generation function */
} stress_zlib_rand_data_info_t;

static const stress_zlib_rand_data_info_t *opt_zlib_rand_data_func;
static const stress_zlib_rand_data_info_t zlib_rand_data_methods[];
static volatile bool pipe_broken = false;

/*
 *  stress_sigpipe_handler()
 *      SIGFPE handler
 */
static void MLOCKED stress_sigpipe_handler(int dummy)
{
	(void)dummy;

	pipe_broken = true;
}

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

/*
 *  stress_fixed_data()
 *	fill buffer with data that is 0x04030201
 */
static void stress_rand_data_fixed(uint32_t *data, const int size)
{
	const int n = size / sizeof(uint32_t);
	register int i;

	for (i = 0; i < n; i++, data++)
		*data = 0x04030201;
}

static const stress_zlib_rand_data_func rand_data_funcs[] = {
	stress_rand_data_rarely_1,
	stress_rand_data_rarely_0,
	stress_rand_data_binary,
	stress_rand_data_text,
	stress_rand_data_01,
	stress_rand_data_digits,
	stress_rand_data_00_ff,
	stress_rand_data_nybble,
	stress_rand_data_fixed
};

/*
 *  stress_zlib_random_test()
 *	randomly select data generation function
 */
static HOT OPTIMIZE3 void stress_zlib_random_test(uint32_t *data, const int size)
{
	rand_data_funcs[mwc32() % SIZEOF_ARRAY(rand_data_funcs)](data, size);
}


/*
 * Table of zlib data methods
 */
static const stress_zlib_rand_data_info_t zlib_rand_data_methods[] = {
	{ "random",	stress_zlib_random_test }, /* Special "random" test */

	{ "binary",	stress_rand_data_binary },
	{ "text",	stress_rand_data_text },
	{ "ascii01",	stress_rand_data_01 },
	{ "asciidigits",stress_rand_data_digits },
	{ "00ff",	stress_rand_data_00_ff },
	{ "nybble",	stress_rand_data_nybble },
	{ "rarely1",	stress_rand_data_rarely_1 },
	{ "rarely0",	stress_rand_data_rarely_0 },
	{ "fixed",	stress_rand_data_fixed },
	{ NULL,		NULL }
};


/*
 *  stress_set_zlib_method()
 *	set the default zlib random data method
 */
int HOT OPTIMIZE3 stress_set_zlib_method(const char *name)
{
	stress_zlib_rand_data_info_t const *info = zlib_rand_data_methods;

	for (info = zlib_rand_data_methods; info->func; info++) {
		if (!strcmp(info->name, name)) {
			opt_zlib_rand_data_func = info;
			return 0;
		}
	}

	fprintf(stderr, "zlib-method must be one of:");
	for (info = zlib_rand_data_methods; info->func; info++) {
		fprintf(stderr, " %s", info->name);
	}
	fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_zlib_err()
 *	turn a zlib error to something human readable
 */
static const char *stress_zlib_err(const int zlib_err)
{
	static char buf[1024];

	switch (zlib_err) {
	case Z_OK:
		return "no error";
	case Z_ERRNO:
		(void)snprintf(buf, sizeof(buf), "system error, errno=%d (%s)\n",
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
		(void)snprintf(buf, sizeof(buf), "unknown zlib error %d\n", zlib_err);
		return buf;
	}
}


/*
 *  stress_zlib_inflate()
 *	inflate compressed data out of the read
 *	end of a pipe fd
 */
static int stress_zlib_inflate(
		const args_t *args,
		const int fd,
		const int xsum_fd)
{
	int ret, err = 0;
	z_stream stream_inf;
	uint64_t xsum = 0, xsum_chars = 0;
	unsigned char in[DATA_SIZE];
	unsigned char out[DATA_SIZE];

	stream_inf.zalloc = Z_NULL;
	stream_inf.zfree = Z_NULL;
	stream_inf.opaque = Z_NULL;
	stream_inf.avail_in = 0;
	stream_inf.next_in = Z_NULL;

	ret = inflateInit(&stream_inf);
	if (ret != Z_OK) {
		pr_fail("%s: zlib inflateInit error: %s\n",
			args->name, stress_zlib_err(ret));
		if (write(xsum_fd, &xsum, sizeof(xsum)) < 0) {
			pr_fail("%s: zlib inflate pipe write error: "
				"errno=%d (%s)\n",
				args->name, err, strerror(err));
		}
		return EXIT_FAILURE;
	}

	do {
		ssize_t sz;

		sz = read(fd, in, DATA_SIZE);
		if (sz < 0) {
			if ((errno != EINTR) && (errno != EPIPE)) {
				pr_fail("%s: zlib inflate pipe read error: "
					"errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)inflateEnd(&stream_inf);
				return EXIT_FAILURE;
			} else {
				break;
			}
		}

		if (sz == 0)
			break;

		stream_inf.avail_in = sz;
		stream_inf.next_in = in;

		do {
			stream_inf.avail_out = DATA_SIZE;
			stream_inf.next_out = out;

			ret = inflate(&stream_inf, Z_NO_FLUSH);
			switch (ret) {
			case Z_NEED_DICT:
			case Z_DATA_ERROR:
			case Z_MEM_ERROR:
				(void)inflateEnd(&stream_inf);
				return EXIT_FAILURE;
			}

			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				size_t i;

				for (i = 0; i < DATA_SIZE - stream_inf.avail_out; i++) {
					xsum += (uint64_t)out[i];
					xsum_chars++;
				}
			}
		} while (stream_inf.avail_out == 0);
	} while (ret != Z_STREAM_END);

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		pr_dbg("%s: inflate xsum value %" PRIu64
			", xsum_chars %" PRIu64 "\n",
			args->name, xsum, xsum_chars);
	}
	(void)inflateEnd(&stream_inf);

	if (write(xsum_fd, &xsum, sizeof(xsum)) < 0) {
		pr_fail("%s: zlib inflate pipe write error: "
			"errno=%d (%s)\n",
			args->name, err, strerror(err));
	}
	return ((ret == Z_OK) || (ret == Z_STREAM_END)) ?
		EXIT_SUCCESS : EXIT_FAILURE;
}

/*
 *  stress_zlib_deflate()
 *	compress random data and write it down the
 *	write end of a pipe fd
 */
static int stress_zlib_deflate(
		const args_t *args,
		const int fd,
		const int xsum_fd)
{
	int ret, err = 0;
	bool do_run;
	z_stream stream_def;
	uint64_t bytes_in = 0, bytes_out = 0, xsum = 0;
	uint64_t xsum_chars = 0;
	int flush = Z_FINISH;

	stream_def.zalloc = Z_NULL;
	stream_def.zfree = Z_NULL;
	stream_def.opaque = Z_NULL;

	ret = deflateInit(&stream_def, Z_BEST_COMPRESSION);
	if (ret != Z_OK) {
		pr_fail("%s: zlib deflateInit error: %s\n",
			args->name, stress_zlib_err(ret));
		if (write(xsum_fd, &xsum, sizeof(xsum)) != sizeof(xsum)) {
			if ((errno != EINTR) && (errno != EPIPE)) {
				pr_fail("%s: zlib deflate pipe write error: "
					"errno=%d (%s)\n",
					args->name, err, strerror(err));
			} else {
				goto finish;
			}
		}
		return EXIT_FAILURE;
	}

	do {
		uint32_t in[DATA_SIZE / sizeof(uint32_t)];
		unsigned char *xsum_in = (unsigned char *)in;

		opt_zlib_rand_data_func->func(in, DATA_SIZE);

		stream_def.avail_in = DATA_SIZE;
		stream_def.next_in = (unsigned char *)in;

		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			for (int i = 0; i < (int)(DATA_SIZE / sizeof(unsigned char)); i++) {
				xsum += (uint64_t)xsum_in[i];
				xsum_chars++;
			}
		}

		do_run = keep_stressing();
		flush = do_run ? Z_NO_FLUSH : Z_FINISH;
		bytes_in += DATA_SIZE;

		do {
			unsigned char out[DATA_SIZE];
			int def_size, rc;

			stream_def.avail_out = DATA_SIZE;
			stream_def.next_out = out;
			rc = deflate(&stream_def, flush);

			if (rc == Z_STREAM_ERROR) {
				pr_fail("%s: zlib deflate error: %s\n",
					args->name, stress_zlib_err(rc));
				(void)deflateEnd(&stream_def);
				return EXIT_FAILURE;
			}
			def_size = DATA_SIZE - stream_def.avail_out;
			bytes_out += def_size;
			if (write(fd, out, def_size) != def_size) {
				if ((errno != EINTR) && (errno != EPIPE)) {
					pr_fail("%s: write error: errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					(void)deflateEnd(&stream_def);
					return EXIT_FAILURE;
				} else {
					(void)deflateEnd(&stream_def);
					goto finish;
				}
			}
			inc_counter(args);
		} while (stream_def.avail_out == 0);
	} while (flush != Z_FINISH);

finish:
	pr_inf("%s: instance %" PRIu32 ": compression ratio: %5.2f%%\n",
		args->name, args->instance,
		bytes_in ? 100.0 * (double)bytes_out / (double)bytes_in : 0);

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		pr_dbg("%s: deflate xsum value %" PRIu64
			", xsum_chars %" PRIu64 "\n",
			args->name, xsum, xsum_chars);
	}

	if (write(xsum_fd, &xsum, sizeof(xsum)) < 0 ) {
		pr_fail("%s: zlib deflate pipe write error: "
			"errno=%d (%s)\n",
			args->name, err, strerror(err));
	}

	(void)deflateEnd(&stream_def);
	return ret;
}

/*
 *  stress_zlib()
 *	stress cpu with compression and decompression
 */
int stress_zlib(const args_t *args)
{
	int ret = EXIT_SUCCESS, fds[2], deflate_xsum_fds[2], inflate_xsum_fds[2], status;
	int err = 0;
	pid_t pid;
	uint64_t deflate_xsum = 0, inflate_xsum = 0;
	ssize_t n;
	bool good_xsum_reads = true;

	if (stress_sighandler(args->name, SIGPIPE, stress_sigpipe_handler, NULL) < 0)
		return EXIT_FAILURE;

	if (pipe(fds) < 0) {
		pr_err("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	if (pipe(deflate_xsum_fds) < 0) {
		pr_err("%s: deflate xsum pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fds[0]);
		(void)close(fds[1]);
		return EXIT_FAILURE;
	}

	if (pipe(inflate_xsum_fds) < 0) {
		pr_err("%s: inflate xsum pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fds[0]);
		(void)close(fds[1]);
		(void)close(deflate_xsum_fds[0]);
		(void)close(deflate_xsum_fds[1]);
		return EXIT_FAILURE;
	}

	pid = fork();
	if (pid < 0) {
		(void)close(fds[0]);
		(void)close(fds[1]);
		(void)close(deflate_xsum_fds[0]);
		(void)close(deflate_xsum_fds[1]);
		(void)close(inflate_xsum_fds[0]);
		(void)close(inflate_xsum_fds[1]);
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		(void)close(fds[1]);
		ret = stress_zlib_inflate(args, fds[0], inflate_xsum_fds[1]);
		(void)close(fds[0]);
		_exit(ret);
	} else {
		(void)close(fds[0]);
		ret = stress_zlib_deflate(args, fds[1], deflate_xsum_fds[1]);
		(void)close(fds[1]);
	}

	n = read(deflate_xsum_fds[0], &deflate_xsum, sizeof(deflate_xsum));
	if (n != sizeof(deflate_xsum)) {
		good_xsum_reads = false;
		if ((errno != EINTR) && (errno != EPIPE)) {
			pr_fail("%s: zlib deflate xsum read pipe error: errno=%d (%s)\n",
				args->name, err, strerror(err));
		}
	}
	n = read(inflate_xsum_fds[0], &inflate_xsum, sizeof(inflate_xsum));
	if (n != sizeof(inflate_xsum)) {
		good_xsum_reads = false;
		if ((errno != EINTR) && (errno != EPIPE)) {
			pr_fail("%s: zlib inflate xsum read pipe error: errno=%d (%s)\n",
				args->name, err, strerror(err));
		}
	}

	if (pipe_broken || !good_xsum_reads) {
		pr_inf("%s: cannot verify inflate/deflate checksums, "
			"interrupted or broken pipe\n", args->name);
	} else {
		if ((g_opt_flags & OPT_FLAGS_VERIFY) && (deflate_xsum != inflate_xsum)) {
			pr_fail("%s: zlib xsum values do NOT match "
				"deflate xsum %" PRIu64
				" vs inflate xsum %" PRIu64 "\n",
				args->name, deflate_xsum, inflate_xsum);
			ret = EXIT_FAILURE;
		}
	}

	(void)close(deflate_xsum_fds[0]);
	(void)close(deflate_xsum_fds[1]);
	(void)close(inflate_xsum_fds[0]);
	(void)close(inflate_xsum_fds[1]);

	(void)kill(pid, SIGKILL);
	(void)waitpid(pid, &status, 0);

	return ret;
}
#else
int stress_zlib(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
