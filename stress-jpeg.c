/*
 * Copyright (C)      2022 Colin Ian King.
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
#include "stress-ng.h"

#if defined(HAVE_LIBJPEG_H)
#include <jpeglib.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"jpeg N",		"start N workers that burn cycles with no-ops" },
	{ NULL,	"jpeg-ops N",		"stop after N jpeg bogo no-op operations" },
	{ NULL,	"jpeg-height N",	"image height in pixels "},
	{ NULL,	"jpeg-width N",		"image width  in pixels "},
	{ NULL,	"jpeg-quality Q",	"compression quality 1 (low) .. 100 (high)" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_set_jpeg_height()
 *      set jpeg height
 */
static int stress_set_jpeg_height(const char *opt)
{
	int32_t jpeg_height;

	jpeg_height = stress_get_uint32(opt);
        stress_check_range("jpeg-height", jpeg_height, 256, 4096);
        return stress_set_setting("jpeg-height", TYPE_ID_INT32, &jpeg_height);
}

/*
 *  stress_set_jpeg_width()
 *      set jpeg width
 */
static int stress_set_jpeg_width(const char *opt)
{
	int32_t jpeg_width;

	jpeg_width = stress_get_uint32(opt);
        stress_check_range("jpeg-width", jpeg_width, 256, 4096);
        return stress_set_setting("jpeg-width", TYPE_ID_INT32, &jpeg_width);
}

/*
 *  stress_set_jpeg_quality()
 *      set jpeg quality 1..100 (100 best)
 */
static int stress_set_jpeg_quality(const char *opt)
{
	int32_t jpeg_quality;

	jpeg_quality = stress_get_uint32(opt);
        stress_check_range("jpeg-quality", jpeg_quality, 1, 100);
        return stress_set_setting("jpeg-quality", TYPE_ID_INT32, &jpeg_quality);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_jpeg_height,	stress_set_jpeg_height },
	{ OPT_jpeg_width,	stress_set_jpeg_width },
	{ OPT_jpeg_quality,	stress_set_jpeg_quality },
	{ 0,			NULL }
};

#if defined(HAVE_LIB_JPEG) &&	\
    defined(HAVE_LIBJPEG_H)

static inline double OPTIMIZE3 plasma(const double x, const double y, const double time)
{
	const double pi2 = 2 * 3.1415;
	double cx, cy;
	double value;

	value = sin((time - x) * pi2);
	value += cos((time + y) * pi2);
	value += sin((time + x - y) * pi2);
	value += sin((time + x + y) * pi2);

	cx = x - 0.5 + sin(time * pi2) / 3;
	cy = y - 0.5 + cos(time * pi2) / 3;
	value += sin(sqrt(128*(cx * cx + cy * cy)));

	return value;
}

static void OPTIMIZE3 stress_rgb_plasma(
	uint8_t		*rgb,
	const int32_t	x_max,
	const int32_t	y_max)
{
	register uint8_t *ptr = rgb;
	register int32_t sy;
	const double tx = (double)stress_mwc32() / 100;
	const double ty = (double)stress_mwc32() / 100;
	const double tz = (double)stress_mwc32() / 100;
	const double dx = 1.0 / (double)x_max;
	const double dy = 1.0 / (double)y_max;
	double y;

	for (y = 0.0, sy = 0; sy < y_max; y += dy, sy++) {
		register int32_t sx;
		double x;

		for (x = 0.0, sx = 0; sx < x_max; x += dx, sx++) {
			*ptr++ = 127 * plasma(x, y, tx) + 127;
			*ptr++ = 127 * plasma(x, y, ty + x) + 127;
			*ptr++ = 127 * plasma(x, y, tz + y) + 127;
		}
	}
}

static int stress_rgb_compress_to_jpeg(
	uint8_t		*rgb,
	const int32_t	x_max,
	const int32_t	y_max,
	int32_t		quality)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPROW row_pointer[y_max];
	int row_stride;
	FILE *fp;
	char *ptr;
	size_t size = 0;
	int32_t y;
	static int32_t yy = 0;

	fp = open_memstream(&ptr, &size);
	if (!fp)
		return -1;

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, fp);

	cinfo.image_width = x_max;
	cinfo.image_height = y_max;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, (int)quality, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

	row_stride = x_max * 3; /* JSAMPLEs per row in image_buffer */
	for (y = 0; y < y_max; y++, rgb += row_stride) {
		yy %= y_max;
		row_pointer[yy] = rgb;
		yy++;
	}
	yy++;

	(void)jpeg_write_scanlines(&cinfo, row_pointer, y_max);
	jpeg_finish_compress(&cinfo);
	(void)fclose(fp);
	jpeg_destroy_compress(&cinfo);
	free(ptr);

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
	uint8_t *rgb;
	double size_compressed = 0.0;
	double size_uncompressed = 0.0;
	double t1, t2, t_jpeg;
	int32_t jpeg_quality = 95;
	size_t rgb_size;

	(void)stress_get_setting("jpeg-width", &x_max);
	(void)stress_get_setting("jpeg-height", &y_max);
	(void)stress_get_setting("jpeg-quality", &jpeg_quality);

	rgb_size = x_max * y_max * 3;
	rgb = mmap(NULL, rgb_size, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (rgb == MAP_FAILED) {
		pr_inf("%s: cannot allocate RGB buffer of size %" PRId32 " x %" PRId32 " x %d, skipping stressor\n",
			args->name, x_max, y_max, 3);
		return EXIT_NO_RESOURCE;
	}

	stress_mwc_set_seed(0xf1379ab2, 0x679ce25d);

	stress_rgb_plasma(rgb, x_max, y_max);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t_jpeg = 0.0;
	do {
		int size;

		t1 = stress_time_now();
		size = stress_rgb_compress_to_jpeg(rgb, x_max, y_max, jpeg_quality);
		t2 = stress_time_now();
		t_jpeg += (t2 - t1);
		if (size > 0) {
			size_uncompressed += (double)rgb_size;
			size_compressed += (double)size;
		}
		inc_counter(args);
	} while (keep_stressing(args));
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	pr_dbg("%s: compressed to %.1f%% of original size, %.2f secs of jpeg compute, %.2f jpegs/sec\n",
		args->name, 100.0 * size_compressed / size_uncompressed,
		t_jpeg, (double)get_counter(args) / t_jpeg);

	(void)munmap((void *)rgb, rgb_size);

	return EXIT_SUCCESS;
}

stressor_info_t stress_jpeg_info = {
	.stressor = stress_jpeg,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_jpeg_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
