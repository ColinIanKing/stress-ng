/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"zlib N",	 "start N workers compressing data with zlib" },
	{ NULL,	"zlib-ops N",	 "stop after N zlib bogo compression operations" },
	{ NULL,	"zlib-level L",	 "specify zlib compression level 0=fast, 9=best" },
	{ NULL,	"zlib-method M", "specify zlib random data generation method M" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_LIB_Z)

#include "zlib.h"

#define DATA_SIZE_1K 	(KB)		/* Must be a multiple of 8 bytes */
#define DATA_SIZE_4K 	(KB * 4)	/* Must be a multiple of 8 bytes */
#define DATA_SIZE_16K 	(KB * 16)	/* Must be a multiple of 8 bytes */
#define DATA_SIZE_64K 	(KB * 64)	/* Must be a multiple of 8 bytes */
#define DATA_SIZE_128K 	(KB * 128)	/* Must be a multiple of 8 bytes */

#define DATA_SIZE DATA_SIZE_64K

typedef void (*stress_zlib_rand_data_func)(const args_t *args, uint32_t *data, const int size);

typedef struct {
	const char *name;			/* human readable form of random data generation selection */
	const stress_zlib_rand_data_func func;	/* the random data generation function */
} stress_zlib_rand_data_info_t;

typedef struct {
	uint64_t	xsum;
	bool		error;
	bool		pipe_broken;
	bool		interrupted;
} xsum_t;
	
static stress_zlib_rand_data_info_t zlib_rand_data_methods[];
static volatile bool pipe_broken = false;
static sigjmp_buf jmpbuf;

static const char *const lorem_ipsum[] = {
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit. ",
	"Nullam imperdiet quam at ultricies finibus. ",
	"Nunc venenatis euismod velit sit amet ornare.",
	"Quisque et orci eu eros convallis luctus at facilisis ex. ",
	"Quisque fringilla nulla felis, sed mollis est feugiat nec. ",
	"Vivamus at urna sit amet velit suscipit iaculis. ",
	"Curabitur mauris ipsum, gravida in laoreet ac, dignissim id lacus. ",
	"Proin dignissim, erat nec interdum commodo, nulla mi tempor dui, quis scelerisque odio nisi in tortor. ",
	"Mauris dignissim ex auctor nulla lobortis semper. ",
	"Mauris sit amet tempus risus, ac tincidunt lectus. ",
	"Maecenas sollicitudin porttitor nisi ac faucibus. ",
	"Cras eu sollicitudin arcu. ",
	"In sed fringilla eros, vitae fringilla tortor. ",
	"Phasellus mollis feugiat tortor, a ornare nunc auctor porttitor. ",
	"Fusce malesuada ut felis vitae vestibulum. ",
	"Donec sit amet hendrerit massa, vitae ultrices augue. ",
	"Proin volutpat velit ipsum, id imperdiet risus placerat ut. ",
	"Cras vitae risus ipsum.  ",
	"Sed lobortis quam in dictum pulvinar. ",
	"In non accumsan justo. ",
	"Ut pretium pulvinar gravida. ",
	"Proin ultricies nisi libero, a convallis dui vestibulum eu. ",
	"Aliquam in molestie magna, et ullamcorper turpis. ",
	"Donec id pharetra sem.  "
	"Duis dui massa, fringilla id mattis nec, consequat eget felis. ",
	"Integer a lobortis ipsum, quis ornare felis. ",
	"Class aptent taciti sociosqu ad litora torquent per conubia nostra, per inceptos himenaeos. ",
	"Nulla sed cursus nibh. ",
	"Quisque at ex dolor. ",
	"Mauris viverra risus pellentesque nisl dictum rutrum. ",
	"Aliquam non est quis enim dictum tristique. ",
	"Fusce feugiat hendrerit hendrerit. ",
	"Ut egestas sed erat et egestas. ",
	"Pellentesque convallis erat sed sapien pellentesque vulputate. ",
	"Praesent non sapien aliquet risus varius suscipit. ",
	"Curabitur eu felis dignissim, hendrerit magna vitae, condimentum nunc. ",
	"Donec ut tincidunt sem. ",
	"Sed in leo et metus ultricies semper quis quis ex. ",
	"Sed fringilla porta mi vitae condimentum. ",
	"In vitae metus libero."
};

/*
 *  stress_sigpipe_handler()
 *      SIGFPE handler
 */
static void MLOCKED_TEXT stress_sigpipe_handler(int signum)
{
	(void)signum;

	pipe_broken = true;
}

static void MLOCKED_TEXT stress_bad_read_handler(int signum)
{
	(void)signum;

	siglongjmp(jmpbuf, 1);
}

/*
 *  stress_rand_data_bcd()
 *	fill buffer with random binary coded decimal digits
 */
static void stress_rand_data_bcd(const args_t *args, uint32_t *data, const int size)
{
	register uint8_t *ptr = (uint8_t *)data;
	const uint8_t *end = ptr + size;

	(void)args;

	while (ptr < end) {
		uint8_t rndval = mwc8() % 100;

		/* Not the most efficient but it works */
		*ptr++ = (rndval % 10) | ((rndval / 10) << 4);
	}
}

/*
 *  stress_rand_data_utf8()
 *	fill buffer with random bytes converted into utf8
 */
static void stress_rand_data_utf8(const args_t *args, uint32_t *data, const int size)
{
	const int n = size / sizeof(uint16_t);
	register uint8_t *ptr = (uint8_t *)data;
	const uint8_t *end = ptr + n;

	(void)args;

	while (ptr < end) {
		uint8_t ch = mwc8();
	
		if (ch <= 0x7f)
			*ptr++ = ch;
		else {
			if (UNLIKELY(ptr < end - 1)) {
				*ptr++ = ((ch >> 6) & 0x1f) | 0xc0;
				*ptr++ = (ch & 0x3f) | 0x80;
			} else {
				/* no more space, zero pad */
				*ptr = 0;
			}
		}
	}
}

/*
 *  stress_rand_data_binary()
 *	fill buffer with random binary data
 */
static void stress_rand_data_binary(const args_t *args, uint32_t *data, const int size)
{
	const int n = size / sizeof(*data);
	register int i;

	(void)args;

	for (i = 0; i < n; i++, data++)
		*data = mwc32();
}

/*
 *  stress_rand_data_text()
 *	fill buffer with random ASCII text
 */
static void stress_rand_data_text(const args_t *args, uint32_t *data, const int size)
{
	(void)args;

	stress_strnrnd((char *)data, size);
}

/*
 *  stress_rand_data_01()
 *	fill buffer with random ASCII 0 or 1
 */
static void stress_rand_data_01(const args_t *args, uint32_t *data, const int size)
{
	unsigned char *ptr = (unsigned char *)data;
	register int i;

	(void)args;

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
static void stress_rand_data_digits(const args_t *args, uint32_t *data, const int size)
{
	unsigned char *ptr = (unsigned char *)data;
	register int i;

	(void)args;

	for (i = 0; i < size; i++, ptr++)
		*ptr = '0' + (mwc32() % 10);
}

/*
 *  stress_rand_data_00_ff()
 *	fill buffer with random 0x00 or 0xff
 */
static void stress_rand_data_00_ff(const args_t *args, uint32_t *data, const int size)
{
	unsigned char *ptr = (unsigned char *)data;
	register int i;

	(void)args;

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
static void stress_rand_data_nybble(const args_t *args, uint32_t *data, const int size)
{
	unsigned char *ptr = (unsigned char *)data;
	register int i;

	(void)args;

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
static void stress_rand_data_rarely_1(const args_t *args, uint32_t *data, const int size)
{
	const int n = size / sizeof(*data);
	register int i;

	(void)args;

	for (i = 0; i < n; i++, data++)
		*data = 1 << (mwc32() & 0x1f);
}

/*
 *  stress_rand_data_rarely_0()
 *	fill buffer with data that is 1 in every 32 bits 0
 */
static void stress_rand_data_rarely_0(const args_t *args, uint32_t *data, const int size)
{
	const int n = size / sizeof(*data);
	register int i;

	(void)args;

	for (i = 0; i < n; i++, data++)
		*data = ~(1 << (mwc32() & 0x1f));
}

/*
 *  stress_rand_data_fixed()
 *	fill buffer with data that is 0x04030201
 */
static void stress_rand_data_fixed(const args_t *args, uint32_t *data, const int size)
{
	const int n = size / sizeof(*data);
	register int i;

	(void)args;

	for (i = 0; i < n; i++, data++)
		*data = 0x04030201;
}

/*
 *  stress_rand_data_double()
 *	fill buffer with double precision floating point binary data
 */
static void stress_rand_data_double(const args_t *args, uint32_t *data, const int size)
{
	const int n = size / sizeof(double);
	uint8_t *ptr = (uint8_t *)data;
	register int i;
	static double theta = 0.0;
	double dtheta = M_PI / 180.0;

	(void)args;

	for (i = 0; i < n; i++) {
		double s = sin(theta);
		(void)memcpy(ptr, &s, sizeof(double));
		theta += dtheta;
		dtheta += 0.001;
		ptr += sizeof(double);
	}
}

/*
 *  stress_rand_data_gray()
 *	fill buffer with gray code of incrementing 16 bit values 
 *
 */
static void stress_rand_data_gray(const args_t *args, uint32_t *data, const int size)
{
	const int n = size / sizeof(uint16_t);
	static uint16_t val = 0;
	register uint16_t v = val;
	register uint16_t *ptr = (uint16_t *)data;
	register int i;

	(void)args;

	for (i = 0; i < n; i++, v++) {
		*(ptr++) = (v >> 1) ^ i;
	}
	val = v;
}


/*
 *  stress_rand_data_parity()
 *	fill buffer with 7 bit data + 1 parity bit
 */
static void stress_rand_data_parity(const args_t *args, uint32_t *data, const int size)
{
	uint8_t *ptr = (uint8_t *)data;
	register int i;

	(void)args;

	for (i = 0; i < size; i++, ptr++) {
		uint8_t v = mwc8();
		uint8_t p = v & 0xfe;
		p ^= p >> 4;
		p &= 0xf;
		p = (0x6996 >> p) & 1;
		*ptr = v | p;
	}
}


#define PINK_MAX_ROWS   (12)
#define PINK_BITS       (16)
#define PINK_SHIFT      ((sizeof(uint64_t) * 8) - PINK_BITS)

#if !defined(HAVE_BUILTIN_CTZ)
/*
 *  stress_builtin_ctz()
 *	implementation of count trailing zeros ctz, this will
 *	be optimized down to 1 instruction on x86 targets
 */
static inline uint32_t stress_builtin_ctz(register uint32_t x)
{
	register unsigned int n;
	if (!x)
		return 32;

	n = 0;
	if ((x & 0x0000ffff) == 0) {
		n += 16;
		x >>= 16;
	}
	if ((x & 0x000000ff) == 0) {
		n += 8;
		x >>= 8;
	}
	if ((x & 0x0000000f) == 0) {
		n += 4;
		x >>= 4;
	}
	if ((x & 0x00000003) == 0) {
		n += 2;
		x >>= 2;
	}
	if ((x & 0x00000001) == 0) {
		n += 1;
	}
	return n;
}
#endif

/*
 *  stress_rand_data_pink()
 *	fill buffer with pink noise 0..255 using an
 *	the Gardner method with the McCartney
 *	selection tree optimization
 */
static void stress_rand_data_pink(const args_t *args, uint32_t *data, const int size)
{
	int i;
	unsigned char *ptr = (unsigned char *)data;
	size_t idx = 0;
	const size_t mask = (1 << PINK_MAX_ROWS) - 1;
	uint64_t sum = 0;
	const uint64_t max = (PINK_MAX_ROWS + 1) * (1 << (PINK_BITS - 1));
	uint64_t rows[PINK_MAX_ROWS];
	const float scalar = 256.0 / max;

	(void)args;
	(void)memset(rows, 0, sizeof(rows));

	for (i = 0; i < size; i++) {
		int64_t rnd;

		idx = (idx + 1) & mask;
		if (idx) {
#if defined(HAVE_BUILTIN_CTZ)
			const size_t j = __builtin_ctz(idx);
#else
			const size_t j = stress_builtin_ctz(idx);
#endif

			sum -= rows[j];
			rnd = (int64_t)mwc64() >> PINK_SHIFT;
			sum += rnd;
			rows[j] = rnd;
		}
		rnd = (int64_t)mwc64() >> PINK_SHIFT;
		*(ptr++) = (int)((scalar * ((int64_t)sum + rnd)) + 128.0);
	}
}

/*
 *  stress_rand_data_brown()
 *	fills buffer with brown noise.
 */
static void stress_rand_data_brown(const args_t *args, uint32_t *data, const int size)
{
	static uint8_t val = 127;
	register int i;
	register uint8_t *ptr = (unsigned char *)data;

	(void)args;

	for (i = 0; i < size; i++) {
		val += ((mwc8() % 31) - 15);
		*(ptr++) = val;
	}
}

/*
 *  stress_rand_data_lrand48()
 *	fills buffer with random data from lrand48
 */
static void stress_rand_data_lrand48(const args_t *args, uint32_t *data, const int size)
{
	static bool seeded = false;
	const int n = size / sizeof(*data);
	register int i;

	if (UNLIKELY(!seeded)) {
		srand48(mwc32());
		seeded = true;
	}

	(void)args;

	for (i = 0; i < n; i++, data++)
		*data = lrand48();
}

/*
 *  stress_rand_data_latin()
 *	fill buffer with random latin Lorum Ipsum text.
 */
static void stress_rand_data_latin(const args_t *args, uint32_t *data, const int size)
{
	register int i;
	static const char *ptr = NULL;
	char *dataptr = (char *)data;

	(void)args;

	if (!ptr)
		ptr = lorem_ipsum[mwc32() % SIZEOF_ARRAY(lorem_ipsum)];

	for (i = 0; i < size; i++) {
		if (!*ptr)
			ptr = lorem_ipsum[mwc32() % SIZEOF_ARRAY(lorem_ipsum)];

		*dataptr++ = *ptr++;
	}
}


/*
 *  stress_rand_data_objcode()
 *	fill buffer with object code data from stress-ng
 */
static void stress_rand_data_objcode(const args_t *args, uint32_t *const data, const int size)
{
	const int n = size / sizeof(*data);
	register int i;
	static bool use_rand_data = false;
	struct sigaction sigsegv_orig, sigbus_orig;
	char *text = NULL, *dataptr, *text_start, *text_end;
	const size_t text_len = stress_text_addr(&text_start, &text_end);

	if (use_rand_data) {
		stress_rand_data_binary(args, data, size);
		return;
	}

	/* Try and install sighandlers */
	if (stress_sighandler(args->name, SIGSEGV, stress_bad_read_handler, &sigsegv_orig) < 0) {
		use_rand_data = true;
		stress_rand_data_binary(args, data, size);
		return;
	}
	if (stress_sighandler(args->name, SIGBUS, stress_bad_read_handler, &sigbus_orig) < 0) {
		use_rand_data = true;
		(void)stress_sigrestore(args->name, SIGSEGV, &sigsegv_orig);
		stress_rand_data_binary(args, data, size);
		return;
	}

	/*
	 *  Some architectures may generate faults on reads
	 *  from specific text pages, so trap these and
	 *  fall back to stress_rand_data_binary.
	 */
	if (sigsetjmp(jmpbuf, 1) != 0) {
		(void)stress_sigrestore(args->name, SIGSEGV, &sigsegv_orig);
		(void)stress_sigrestore(args->name, SIGBUS, &sigbus_orig);
		stress_rand_data_binary(args, data, size);
		return;
	}

	/* Start in random place in stress-ng text segment */
	text = text_len ? text_start + (mwc64() % text_len) : text_start;

	for (dataptr = (char *)data, i = 0; i < n; i++, dataptr++) {
		*dataptr = *text;
		if (text++ >= text_end)
			text = text_start;
	}
	(void)stress_sigrestore(args->name, SIGSEGV, &sigsegv_orig);
	(void)stress_sigrestore(args->name, SIGBUS, &sigbus_orig);
}

/*
 *  stress_rand_data_zero()
 *	fill buffer with zeros
 */
static void stress_rand_data_zero(const args_t *args, uint32_t *data, const int size)
{
	(void)args;
	(void)memset(data, 0, size);
}


static const stress_zlib_rand_data_func rand_data_funcs[] = {
	stress_rand_data_00_ff,
	stress_rand_data_01,
	stress_rand_data_digits,
	stress_rand_data_bcd,
	stress_rand_data_binary,
	stress_rand_data_brown,
	stress_rand_data_double,
	stress_rand_data_fixed,
	stress_rand_data_gray,
	stress_rand_data_latin,
	stress_rand_data_lrand48,
	stress_rand_data_nybble,
	stress_rand_data_objcode,
	stress_rand_data_parity,
	stress_rand_data_pink,
	stress_rand_data_rarely_1,
	stress_rand_data_rarely_0,
	stress_rand_data_text,
	stress_rand_data_utf8,
	stress_rand_data_zero,
};

/*
 *  stress_zlib_random_test()
 *	randomly select data generation function
 */
static HOT OPTIMIZE3 void stress_zlib_random_test(const args_t *args, uint32_t *data, const int size)
{
	rand_data_funcs[mwc32() % SIZEOF_ARRAY(rand_data_funcs)](args, data, size);
}


/*
 * Table of zlib data methods
 */
static stress_zlib_rand_data_info_t zlib_rand_data_methods[] = {
	{ "random",	stress_zlib_random_test }, /* Special "random" test */

	{ "00ff",	stress_rand_data_00_ff },
	{ "ascii01",	stress_rand_data_01 },
	{ "asciidigits",stress_rand_data_digits },
	{ "bcd",	stress_rand_data_bcd },
	{ "binary",	stress_rand_data_binary },
	{ "brown",	stress_rand_data_brown },
	{ "double",	stress_rand_data_double },
	{ "gray",	stress_rand_data_gray },
	{ "fixed",	stress_rand_data_fixed },
	{ "latin",	stress_rand_data_latin },
	{ "lrand48",	stress_rand_data_lrand48 },
	{ "nybble",	stress_rand_data_nybble },
	{ "objcode",	stress_rand_data_objcode },
	{ "parity",	stress_rand_data_parity },
	{ "pink",	stress_rand_data_pink },
	{ "rarely1",	stress_rand_data_rarely_1 },
	{ "rarely0",	stress_rand_data_rarely_0 },
	{ "text",	stress_rand_data_text },
	{ "utf8",	stress_rand_data_utf8 },
	{ "zero",	stress_rand_data_zero },
	{ NULL,		NULL }
};

/*
 *  stress_set_zlib_level
 *	set zlib compression level, 0..9,
 *	0 = no compression, 1 = fastest, 9 = best compression
 */
static int stress_set_zlib_level(const char *opt)
{
        uint32_t zlib_level;

        zlib_level = get_uint32(opt);
        check_range("zlib-level", zlib_level, 0, Z_BEST_COMPRESSION);
        return set_setting("zlib-level", TYPE_ID_UINT32, &zlib_level);
}

/*
 *  stress_set_zlib_method()
 *	set the default zlib random data method
 */
static int stress_set_zlib_method(const char *opt)
{
	stress_zlib_rand_data_info_t *info;

	for (info = zlib_rand_data_methods; info->func; info++) {
		if (!strcmp(info->name, opt)) {
			set_setting("zlib-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "zlib-method must be one of:");
	for (info = zlib_rand_data_methods; info->func; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_zlib_level,	stress_set_zlib_level },
	{ OPT_zlib_method,	stress_set_zlib_method },
	{ 0,			NULL }
};

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
	uint64_t xsum_chars = 0;
	static unsigned char in[DATA_SIZE];
	static unsigned char out[DATA_SIZE];
	xsum_t xsum;

	xsum.xsum = 0;
	xsum.error = false;
	xsum.pipe_broken = false;
	xsum.interrupted = false;

	stream_inf.zalloc = Z_NULL;
	stream_inf.zfree = Z_NULL;
	stream_inf.opaque = Z_NULL;
	stream_inf.avail_in = 0;
	stream_inf.next_in = Z_NULL;

	ret = inflateInit(&stream_inf);
	if (ret != Z_OK) {
		pr_fail("%s: zlib inflateInit error: %s\n",
			args->name, stress_zlib_err(ret));
		xsum.error = true;
		goto xsum_error;
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
				xsum.error = true;
				goto xsum_error;
			} else {
				if (errno == EINTR)
					xsum.interrupted = true;
		 		if (errno == EPIPE)
					xsum.pipe_broken = true;
				break;
			}
		}

		if (sz == 0) {
			xsum.pipe_broken = true;
			break;
		}

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
				goto xsum_error;
			}

			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				size_t i;

				for (i = 0; i < DATA_SIZE - stream_inf.avail_out; i++) {
					xsum.xsum += (uint64_t)out[i];
					xsum_chars++;
				}
			}
		} while (stream_inf.avail_out == 0);
	} while (ret != Z_STREAM_END);

	/*
	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		pr_dbg("%s: inflate xsum value %" PRIu64
			", xsum_chars %" PRIu64 "\n",
			args->name, xsum.xsum, xsum_chars);
	}
	*/
	(void)inflateEnd(&stream_inf);

	if (write(xsum_fd, &xsum, sizeof(xsum)) < 0) {
		pr_fail("%s: zlib inflate pipe write error: "
			"errno=%d (%s)\n",
			args->name, err, strerror(err));
	}
	return ((ret == Z_OK) || (ret == Z_STREAM_END)) ?
		EXIT_SUCCESS : EXIT_FAILURE;

xsum_error:
	xsum.error = true;
	if (write(xsum_fd, &xsum, sizeof(xsum)) < 0) {
		pr_fail("%s: zlib inflate pipe write error: "
			"errno=%d (%s)\n",
			args->name, err, strerror(err));
	}
	return EXIT_FAILURE;
}

/*
 *  stress_zlib_deflate()
 *	compress random data and write it down the
 *	write end of a pipe fd
 */
static int stress_zlib_deflate(
		const args_t *args,
		const int fd,
		const int xsum_fd,
		const int zlib_level)
{
	int ret, err = 0;
	bool do_run;
	z_stream stream_def;
	uint64_t bytes_in = 0, bytes_out = 0;
	uint64_t xsum_chars = 0;
	int flush;
	stress_zlib_rand_data_info_t *opt_zlib_rand_data_func = &zlib_rand_data_methods[0];
	double t1, t2;
	xsum_t xsum;

	(void)get_setting("zlib-method", &opt_zlib_rand_data_func);

	xsum.xsum = 0;
	xsum.error = false;
	xsum.pipe_broken = false;
	xsum.interrupted = false;

	stream_def.zalloc = Z_NULL;
	stream_def.zfree = Z_NULL;
	stream_def.opaque = Z_NULL;

	t1 = time_now();
	ret = deflateInit(&stream_def, zlib_level);
	if (ret != Z_OK) {
		pr_fail("%s: zlib deflateInit error: %s\n",
			args->name, stress_zlib_err(ret));
		xsum.error = true;
		ret = EXIT_FAILURE;
		goto xsum_error;
	}

	do {
		static uint32_t in[DATA_SIZE / sizeof(uint32_t)];
		unsigned char *xsum_in = (unsigned char *)in;

		opt_zlib_rand_data_func->func(args, in, DATA_SIZE);

		stream_def.avail_in = DATA_SIZE;
		stream_def.next_in = (unsigned char *)in;

		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			for (int i = 0; i < (int)(DATA_SIZE / sizeof(unsigned char)); i++) {
				xsum.xsum += (uint64_t)xsum_in[i];
				xsum_chars++;
			}
		}

		do_run = keep_stressing();
		flush = do_run ? Z_NO_FLUSH : Z_FINISH;
		bytes_in += DATA_SIZE;

		do {
			static unsigned char out[DATA_SIZE];
			int def_size, rc;

			stream_def.avail_out = DATA_SIZE;
			stream_def.next_out = out;
			rc = deflate(&stream_def, flush);

			if (rc == Z_STREAM_ERROR) {
				pr_fail("%s: zlib deflate error: %s\n",
					args->name, stress_zlib_err(rc));
				(void)deflateEnd(&stream_def);
				ret = EXIT_FAILURE;
				goto xsum_error;
			}
			def_size = DATA_SIZE - stream_def.avail_out;
			bytes_out += def_size;
			if (write(fd, out, def_size) != def_size) {
				if ((errno != EINTR) && (errno != EPIPE) && (errno != 0)) {
					pr_fail("%s: write error: errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					(void)deflateEnd(&stream_def);
					ret = EXIT_FAILURE;
					goto xsum_error;
				} else {
					if (errno == EINTR)
						xsum.interrupted = true;
 					if (errno == EPIPE)
						xsum.pipe_broken = true;
					goto finish;
				}
			}
			inc_counter(args);
		} while (stream_def.avail_out == 0);
	} while (flush != Z_FINISH);

finish:
	t2 = time_now();
	pr_inf("%s: instance %" PRIu32 ": compression ratio: %5.2f%% (%.2f MB/sec)\n",
		args->name, args->instance,
		bytes_in ? 100.0 * (double)bytes_out / (double)bytes_in : 0,
		(t2 - t1 > 0.0) ? (bytes_in / (t2 - t1)) / MB : 0.0);

	/*
	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		pr_dbg("%s: deflate xsum value %" PRIu64
			", xsum_chars %" PRIu64 "\n",
			args->name, xsum.xsum, xsum_chars);
	}
	*/

	(void)deflateEnd(&stream_def);
	ret = EXIT_SUCCESS;
xsum_error:
	if (write(xsum_fd, &xsum, sizeof(xsum)) < 0 ) {
		pr_fail("%s: zlib deflate pipe write error: "
			"errno=%d (%s)\n",
			args->name, err, strerror(err));
	}

	return ret;
}

/*
 *  stress_zlib()
 *	stress cpu with compression and decompression
 */
static int stress_zlib(const args_t *args)
{
	int ret = EXIT_SUCCESS, fds[2], deflate_xsum_fds[2], inflate_xsum_fds[2], status;
	int err = 0;
	pid_t pid;
	xsum_t deflate_xsum, inflate_xsum;
	uint32_t zlib_level = Z_BEST_COMPRESSION;	/* best compression */
	ssize_t n;
	bool bad_xsum_reads = false;
	bool error = false;
	bool interrupted = false;

	(void)memset(&deflate_xsum, 0, sizeof(deflate_xsum));
	(void)memset(&inflate_xsum, 0, sizeof(inflate_xsum));

	if (stress_sighandler(args->name, SIGPIPE, stress_sigpipe_handler, NULL) < 0)
		return EXIT_FAILURE;

	(void)get_setting("zlib-level", &zlib_level);

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
		ret = stress_zlib_deflate(args, fds[1], deflate_xsum_fds[1], (int)zlib_level);
		(void)close(fds[1]);
	}

	n = read(deflate_xsum_fds[0], &deflate_xsum, sizeof(deflate_xsum));
	if (n != sizeof(deflate_xsum)) {
		bad_xsum_reads = true;
		if ((errno != EINTR) && (errno != EPIPE)) {
			pr_fail("%s: zlib deflate xsum read pipe error: errno=%d (%s)\n",
				args->name, err, strerror(err));
		}
	} else {
		pipe_broken |= deflate_xsum.pipe_broken;
		interrupted |= deflate_xsum.interrupted;
		error       |= deflate_xsum.error;
	}

	n = read(inflate_xsum_fds[0], &inflate_xsum, sizeof(inflate_xsum));
	if (n != sizeof(inflate_xsum)) {
		bad_xsum_reads = true;
		if ((errno != EINTR) && (errno != EPIPE)) {
			pr_fail("%s: zlib inflate xsum read pipe error: errno=%d (%s)\n",
				args->name, err, strerror(err));
		}
	} else {
		pipe_broken |= inflate_xsum.pipe_broken;
		interrupted |= inflate_xsum.interrupted;
		error       |= inflate_xsum.error;
	}

	if (pipe_broken || bad_xsum_reads || interrupted || error) {
		pr_inf("%s: cannot verify inflate/deflate checksums:%s%s%s%s%s%s%s\n",
			args->name,
			interrupted ? " interrupted" : "",
			(interrupted & pipe_broken) ? " and" : "",
			pipe_broken ? " broken pipe" : "",
			(((interrupted | pipe_broken)) & error) ? " and" : "",
			error ? " unexpected error" : "",
			((interrupted | pipe_broken | error) & bad_xsum_reads) ? " and" : "",
			bad_xsum_reads ? " could not read checksums" : "");
	} else {
		if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
		    (deflate_xsum.xsum != inflate_xsum.xsum)) {
			pr_fail("%s: zlib xsum values do NOT match "
				"deflate xsum %" PRIu64
				" vs inflate xsum %" PRIu64 "\n",
				args->name, deflate_xsum.xsum, inflate_xsum.xsum);
			ret = EXIT_FAILURE;
		}
	}

	(void)close(deflate_xsum_fds[0]);
	(void)close(deflate_xsum_fds[1]);
	(void)close(inflate_xsum_fds[0]);
	(void)close(inflate_xsum_fds[1]);

	(void)kill(pid, SIGKILL);
	(void)shim_waitpid(pid, &status, 0);

	return ret;
}

static void stress_zlib_set_default(void)
{
	stress_set_zlib_method("random");
}

stressor_info_t stress_zlib_info = {
	.stressor = stress_zlib,
	.set_default = stress_zlib_set_default,
	.class = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_zlib_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.help = help
};
#endif
