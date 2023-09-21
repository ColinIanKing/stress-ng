// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-pragma.h"

#if defined(HAVE_LIBJPEG_H)
#include <jpeglib.h>
#endif

#define JPEG_IMAGE_PLASMA	(0x00)
#define JPEG_IMAGE_NOISE	(0x01)
#define JPEG_IMAGE_GRADIENT	(0x02)
#define JPEG_IMAGE_XSTRIPES	(0x03)
#define JPEG_IMAGE_FLAT		(0x04)
#define JPEG_IMAGE_BROWN	(0x05)

typedef struct {
	const char *name;
	const int  type;
} jpeg_image_type_t;

static const stress_help_t help[] = {
	{ NULL,	"jpeg N",		"start N workers that burn cycles with no-ops" },
	{ NULL,	"jpeg-height N",	"image height in pixels "},
	{ NULL,	"jpeg-image type",	"image type: one of brown, flat, gradient, noise, plasma or xstripes" },
	{ NULL,	"jpeg-ops N",		"stop after N jpeg bogo no-op operations" },
	{ NULL,	"jpeg-quality Q",	"compression quality 1 (low) .. 100 (high)" },
	{ NULL,	"jpeg-width N",		"image width in pixels "},
	{ NULL,	NULL,			NULL }
};

static const jpeg_image_type_t jpeg_image_types[] = {
	{ "brown",	JPEG_IMAGE_BROWN },
	{ "flat",	JPEG_IMAGE_FLAT },
	{ "gradient",	JPEG_IMAGE_GRADIENT },
	{ "noise",	JPEG_IMAGE_NOISE },
	{ "plasma",	JPEG_IMAGE_PLASMA },
	{ "xstripes",	JPEG_IMAGE_XSTRIPES },
};

/*
 *  stress_set_jpeg_height()
 *      set jpeg height
 */
static int stress_set_jpeg_height(const char *opt)
{
	int32_t jpeg_height;

	jpeg_height = stress_get_int32(opt);
	stress_check_range("jpeg-height", (uint64_t)jpeg_height, 256, 4096);
	return stress_set_setting("jpeg-height", TYPE_ID_INT32, &jpeg_height);
}

/*
 *  stress_set_jpeg_width()
 *      set jpeg width
 */
static int stress_set_jpeg_width(const char *opt)
{
	int32_t jpeg_width;

	jpeg_width = stress_get_int32(opt);
	stress_check_range("jpeg-width", (uint64_t)jpeg_width, 256, 4096);
	return stress_set_setting("jpeg-width", TYPE_ID_INT32, &jpeg_width);
}

/*
 *  stress_set_jpeg_quality()
 *      set jpeg quality 1..100 (100 best)
 */
static int stress_set_jpeg_quality(const char *opt)
{
	int32_t jpeg_quality;

	jpeg_quality = stress_get_int32(opt);
	stress_check_range("jpeg-quality", (uint64_t)jpeg_quality, 1, 100);
	return stress_set_setting("jpeg-quality", TYPE_ID_INT32, &jpeg_quality);
}

/*
 *  stress_set_jpeg_image()
 *      set image to compress
 */
static int stress_set_jpeg_image(const char *opt)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(jpeg_image_types); i++) {
		if (strcmp(opt, jpeg_image_types[i].name) == 0)
			return stress_set_setting("jpeg-image", TYPE_ID_INT, &jpeg_image_types[i].type);
	}
	(void)fprintf(stderr, "jpeg-image must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(jpeg_image_types); i++)
		(void)fprintf(stderr, " %s", jpeg_image_types[i].name);

	(void)fprintf(stderr, "\n");

	return -1;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_jpeg_height,	stress_set_jpeg_height },
	{ OPT_jpeg_image,	stress_set_jpeg_image },
	{ OPT_jpeg_width,	stress_set_jpeg_width },
	{ OPT_jpeg_quality,	stress_set_jpeg_quality },
	{ 0,			NULL }
};

#if defined(HAVE_LIB_JPEG) &&	\
    defined(HAVE_LIBJPEG_H)

static inline double OPTIMIZE3 plasma(const double x, const double y, const double time)
{
	const double tau = 2 * M_PI;
	double cx, cy;
	double value;

	value = shim_sin((time - x) * tau);
	value += shim_cos((time + y) * tau);
	value += shim_sin((time + x - y) * tau);
	value += shim_sin((time + x + y) * tau);

	cx = x - 0.5 + shim_sin(time * tau) / 3.0;
	cy = y - 0.5 + shim_cos(time * tau) / 3.0;
	value += shim_sin(shim_sqrt(128.0 * (cx * cx + cy * cy)));

	return value;
}

static void OPTIMIZE3 stress_rgb_plasma(
	uint8_t		*rgb,
	const int32_t	x_max,
	const int32_t	y_max)
{
	register uint8_t *ptr = rgb;
	register int32_t sy;
	const double tx = ((double)stress_mwc32()) / 100.0;
	const double ty = ((double)stress_mwc32()) / 100.0;
	const double tz = ((double)stress_mwc32()) / 100.0;
	const double dx = 1.0 / (double)x_max;
	const double dy = 1.0 / (double)y_max;
	double y;

	for (y = 0.0, sy = 0; sy < y_max; y += dy, sy++) {
		register int32_t sx;
		double x;

		for (x = 0.0, sx = 0; sx < x_max; x += dx, sx++) {
			*ptr++ = (uint8_t)(127.0 * plasma(x, y, tx) + 127.0);
			*ptr++ = (uint8_t)(127.0 * plasma(x, y, ty + x) + 127.0);
			*ptr++ = (uint8_t)(127.0 * plasma(x, y, tz + y) + 127.0);
		}
	}
}

static void OPTIMIZE3 stress_rgb_noise(
	uint8_t		*rgb,
	const int32_t	x_max,
	const int32_t	y_max)
{
	int32_t i, size = x_max * y_max * 3, n;
	uint32_t *ptr32 = (uint32_t *)rgb;
	uint8_t *ptr8;

	n = size >> 2;
	for (i = 0; i < n; i++) {
		*ptr32++ = stress_mwc32();
	}
	n = size & 3;
	ptr8 = (uint8_t *)ptr32;
	for (i = 0; i < n; i++) {
		*ptr8++ = stress_mwc8();
	}
}

static void OPTIMIZE3 stress_rgb_brown(
	uint8_t		*rgb,
	const int32_t	x_max,
	const int32_t	y_max)
{
	int32_t i, size = x_max * y_max * 3;
	uint8_t *ptr = (uint8_t *)rgb;
	const uint32_t val = stress_mwc32();
	register uint8_t r = (val >> 24) & 0xff;
	register uint8_t g = (val >> 16) & 0xff;
	register uint8_t b = (val >> 8) & 0xff;

	for (i = 0; i < size; i++) {
		const uint8_t v = stress_mwc8();

		*ptr++ = r;
		*ptr++ = g;
		*ptr++ = b;

		r += (v & 7) - 3;
		g += ((v >> 3) & 7) - 3;
		b += ((v >> 6) & 3) - 1;
	}
}


static void OPTIMIZE3 stress_rgb_gradient(
	uint8_t		*rgb,
	const int32_t	x_max,
	const int32_t	y_max)
{
	double y = 0.0, dy = 256.0 / y_max;
	register int sy;

	for (sy = 0; sy < y_max; sy++, y += dy) {
		double x = 0.0, dx = 256.0 / x_max;
		register int sx;

		for (sx = 0; sx < x_max; sx++, x += dx) {
			*rgb++ = (uint8_t)x;
			*rgb++ = (uint8_t)y;
			*rgb++ = (uint8_t)(x + y);
		}
	}
}

static void OPTIMIZE3 stress_rgb_xstripes(
	uint8_t		*rgb,
	const int32_t	x_max,
	const int32_t	y_max)
{
	register int y;

	for (y = 0; y < y_max; y++) {
		const uint32_t v = stress_mwc32();
		const uint8_t r = v & 0xff;
		const uint8_t g = (v >> 8) & 0xff;
		const uint8_t b = (v >> 16) & 0xff;
		register int x;

		for (x = 0; x < x_max; x++) {
			*rgb++ = r;
			*rgb++ = g;
			*rgb++ = b;
		}
	}
}

static void OPTIMIZE3 stress_rgb_flat(
	uint8_t		*rgb,
	const int32_t	x_max,
	const int32_t	y_max)
{
	const uint32_t v = stress_mwc32();
	const uint8_t r = v & 0xff;
	const uint8_t g = (v >> 8) & 0xff;
	const uint8_t b = (v >> 16) & 0xff;
	register int y;

	for (y = 0; y < y_max; y++) {
		register int x;

		for (x = 0; x < x_max; x++) {
			*rgb++ = r;
			*rgb++ = g;
			*rgb++ = b;
		}
	}
}

#if defined(HAVE_OPEN_MEMSTREAM)
/*
 *  stress_jpeg_checksum_data()
 *	generate a 32 bit checksum on the jpeg compressed data
 */
static void OPTIMIZE3 stress_jpeg_checksum_data(char *data, const size_t size, uint32_t *checksum)
{
	register uint32_t sum = 0;
	register uint8_t *ptr = (uint8_t *)data;
	register const uint8_t *end = ptr + size;

	while (ptr < end) {
		sum ^= (uint8_t)*ptr;
		ptr++;
		sum = shim_ror32(sum);
	}
	*checksum = sum;
}
#endif

static int stress_rgb_compress_to_jpeg(
	uint8_t		*rgb,
	JSAMPROW 	*row_pointer,
	const int32_t	x_max,
	const int32_t	y_max,
	const int32_t	quality,
	int32_t		*yy,
	bool		verify,
	uint32_t	*checksum,
	double		*duration)
{
	double t1, t2;
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	FILE *fp;
#if defined(HAVE_OPEN_MEMSTREAM)
	char *ptr;
#endif
	size_t size = 0;
	int32_t y;
	const int row_stride = x_max * 3;

	*checksum = 0;
	*duration = 0.0;

	if (y_max < 1)
		return 0;

#if defined(HAVE_OPEN_MEMSTREAM)
	fp = open_memstream(&ptr, &size);
#else
	fp = fopen("/dev/null", "w");
#endif
	if (!fp)
		return -1;

	t1 = stress_time_now();
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, fp);

	cinfo.image_width = (JDIMENSION)x_max;
	cinfo.image_height = (JDIMENSION)y_max;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, (int)quality, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

PRAGMA_UNROLL_N(8)
	for (y = 0; y < y_max; y++, rgb += row_stride) {
		*yy %= y_max;
		row_pointer[*yy] = rgb;
		(*yy)++;
	}

	(void)jpeg_write_scanlines(&cinfo, row_pointer, (JDIMENSION)y_max);
	jpeg_finish_compress(&cinfo);
	(void)fclose(fp);
	jpeg_destroy_compress(&cinfo);
	t2 = stress_time_now();
#if defined(HAVE_OPEN_MEMSTREAM)
	if (verify)
		stress_jpeg_checksum_data(ptr, size, checksum);
	free(ptr);
#else
	(void)verify;
#endif
	*duration = t2 - t1;
	return (int)size;
}

/*
 *  stress_jpeg()
 *	stress jpeg compression
 */
static int stress_jpeg(const stress_args_t *args)
{
	int32_t x_max = 512;
	int32_t y_max = 512;
	uint64_t pixels;
	uint8_t *rgb;
	JSAMPROW *row_pointer;
	double size_compressed = 0.0;
	double size_uncompressed = 0.0;
	double t_jpeg;
	int32_t jpeg_quality = 95;
	int32_t yy = 0;
	size_t rgb_size, row_pointer_size;
	int jpeg_image = JPEG_IMAGE_PLASMA;
	double total_pixels = 0.0, t_start, duration, rate, ratio;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	(void)stress_get_setting("jpeg-width", &x_max);
	(void)stress_get_setting("jpeg-height", &y_max);
	(void)stress_get_setting("jpeg-quality", &jpeg_quality);
	(void)stress_get_setting("jpeg-image", &jpeg_image);

	rgb_size = (size_t)x_max * (size_t)y_max * 3;
	rgb = mmap(NULL, rgb_size, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (rgb == MAP_FAILED) {
		pr_inf_skip("%s: cannot allocate RGB buffer of size %" PRId32 " x %" PRId32 " x %d, skipping stressor\n",
			args->name, x_max, y_max, 3);
		return EXIT_NO_RESOURCE;
	}
	row_pointer_size = (size_t)y_max * sizeof(*row_pointer);
	row_pointer = (JSAMPROW *)mmap(NULL, row_pointer_size, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (row_pointer == MAP_FAILED) {
		pr_inf_skip("%s: cannot allocate row pointer array of size %" PRId32 " x %zu, skipping stressor\n",
			args->name, y_max, sizeof(*row_pointer));
		(void)munmap(rgb, rgb_size);
		return EXIT_NO_RESOURCE;
	}

	stress_mwc_set_seed(0xf1379ab2, 0x679ce25d);

	switch (jpeg_image) {
	default:
	case JPEG_IMAGE_PLASMA:
		stress_rgb_plasma(rgb, x_max, y_max);
		break;
	case JPEG_IMAGE_NOISE:
		stress_rgb_noise(rgb, x_max, y_max);
		break;
	case JPEG_IMAGE_GRADIENT:
		stress_rgb_gradient(rgb, x_max, y_max);
		break;
	case JPEG_IMAGE_XSTRIPES:
		stress_rgb_xstripes(rgb, x_max, y_max);
		break;
	case JPEG_IMAGE_FLAT:
		stress_rgb_flat(rgb, x_max, y_max);
		break;
	case JPEG_IMAGE_BROWN:
		stress_rgb_brown(rgb, x_max, y_max);
		break;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t_jpeg = 0.0;
	t_start = stress_time_now();
	pixels = (uint64_t)x_max * (uint64_t)y_max;
	do {
		int size;
		uint32_t checksum;

		size = stress_rgb_compress_to_jpeg(rgb, row_pointer, x_max, y_max, jpeg_quality, &yy, verify, &checksum, &duration);
		t_jpeg += duration;
		if (size > 0) {
			size_uncompressed += (double)rgb_size;
			size_compressed += (double)size;
			total_pixels += (double)pixels;
		}
		stress_bogo_inc(args);

		if (verify) {
			uint32_t checksum_verify;

			size = stress_rgb_compress_to_jpeg(rgb, row_pointer, x_max, y_max, jpeg_quality, &yy, verify, &checksum_verify, &duration);
			t_jpeg += duration;
			if (size > 0) {
				size_uncompressed += (double)rgb_size;
				size_compressed += (double)size;
				total_pixels += (double)pixels;
			}
			stress_bogo_inc(args);
		}
		yy++;
	} while (stress_continue(args));
	duration = stress_time_now() - t_start;

	rate = (duration > 0) ? total_pixels / duration : 0.0;
	stress_metrics_set(args, 0, "megapixels compressed per sec" , rate / 1000000.0);
	ratio = (size_uncompressed > 0) ? 100.0 * (double)size_compressed / (double)size_uncompressed : 0.0;
	stress_metrics_set(args, 1, "% compression ratio" , ratio);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if ((size_compressed > 0) && (size_uncompressed > 0)) {
		pr_dbg("%s: compressed to %.1f%% of original size, %.2f secs of jpeg compute, %.2f jpegs/sec\n",
			args->name, ratio, t_jpeg, (double)stress_bogo_get(args) / t_jpeg);
	}

	(void)munmap((void *)row_pointer, row_pointer_size);
	(void)munmap((void *)rgb, rgb_size);

	return EXIT_SUCCESS;
}

stressor_info_t stress_jpeg_info = {
	.stressor = stress_jpeg,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
stressor_info_t stress_jpeg_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without jpeg library"
};
#endif
