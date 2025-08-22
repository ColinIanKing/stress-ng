/*
 * Copyright (C) 2024-2025 Colin Ian King
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
#include "core-mmap.h"
#include "core-target-clones.h"

#define MIN_FRACTAL_ITERATIONS	(1)
#define MAX_FRACTAL_ITERATIONS	(65535)

#define MIN_FRACTAL_XSIZE	(64)
#define MAX_FRACTAL_XSIZE	(1000000)

#define MIN_FRACTAL_YSIZE	(64)
#define MAX_FRACTAL_YSIZE	(1000000)

/*
 *  Generate fractals in rows, split the row computation
 *  amongst stressor with the next row to be computed shared
 *  using a fast (spin) lock between instances. Each instance
 *  dumps data into a buffer of columns for that 1 row. There
 *  is NOT final output image, just a per row slice. The
 *  idea is to just stash a row at time to include store latencies
 *  but not have to worry about allocating an entire 2d fractal
 *  rendered image.
 */

typedef struct {
	double xmin;		/* left min */
	double xmax;		/* right max */
	double ymin;		/* bottom min */
	double ymax;		/* top max */
	double dx;		/* x step */
	double dy;		/* y step */
	uint16_t *data;		/* row of data */
	int32_t xsize;		/* width */
	int32_t ysize;		/* height */
	int32_t iterations;	/* loops */
} fractal_info_t;

typedef void (*fractal_func)(fractal_info_t *info, const int32_t row);

typedef struct {
	const char *name;		/* fractal name */
	const fractal_func func;	/* fractal function */
	const fractal_info_t info;	/* fractal data */
} stress_fractal_method_t;

static const stress_help_t help[] = {
	{ NULL,	"fractal N",		"start N workers performing large integer fractalization" },
	{ NULL,	"fractal-iterations N",	"number of iterations" },
	{ NULL,	"fractal-method M",	"fractal method [ mandelbrot | julia ]" },
	{ NULL,	"fractal-ops N",	"stop after N fractalisation operations" },
	{ NULL,	"fractal-xsize N",	"width of fractal" },
	{ NULL,	"fractal-ysize N",	"height of fractal" },
	{ NULL,	NULL,		 	NULL }
};

static void stress_fractal_init(const uint32_t instances)
{
	(void)instances;

	g_shared->fractal.lock = stress_lock_create("fractal");
	g_shared->fractal.row = 0;

	if (g_shared->fractal.lock == NULL)
		printf("fractal lock create failed\n");
}

static void stress_fractal_deinit(void)
{
	if (g_shared->fractal.lock) {
		stress_lock_destroy(g_shared->fractal.lock);
		g_shared->fractal.lock = NULL;
	}
}

/*
 *  stress_fractal_get_row()
 *	get next row to be computed, will wrap around. Wrap arounds
 *	bump the bogo-counter to keep track of entire fractals being
 *	generated
 */
static inline ALWAYS_INLINE int32_t stress_fractal_get_row(stress_args_t *args, int32_t max_rows)
{
#if defined(HAVE_ATOMIC_FETCH_ADD_4) &&	\
    defined(__ATOMIC_ACQUIRE) &&	\
    !defined(__fiwix__)
	/*
	 *  Fast method, inc and modulo. There is an issue where
	 *  the row eventualy wraps and the next row will be incorrect
	 *  if max_rows does not divide exactly into row. However, since
	 *  this is just a compute benchmark, this is a minor issue.
	 */
	register int32_t row = __atomic_fetch_add_4(&g_shared->fractal.row, 1, __ATOMIC_ACQUIRE) % max_rows;

	if (UNLIKELY(row == 0))
		stress_bogo_inc(args);
	return row;
#else
	/*
	 *  Slow method, always correct but much slower as it requires
	 *  a lock.
	 */
	int32_t row, row_next;

	if (UNLIKELY(stress_lock_acquire_relax(g_shared->fractal.lock) < 0))
		return -1;
	row = g_shared->fractal.row;
	row_next = row + 1;
	if (UNLIKELY(row_next >= max_rows))
		row_next = 0;
	g_shared->fractal.row = row_next;
	if (UNLIKELY(stress_lock_release(g_shared->fractal.lock) < 0))
		return -1;
	if (UNLIKELY(row == 0))
		stress_bogo_inc(args);
	return row;
#endif
}

/*
 *  stress_fractal_mandelbrot()
 *	classic Mandlebot generator, naive method, unrolled x 2
 */
static void OPTIMIZE3 TARGET_CLONES stress_fractal_mandelbrot(fractal_info_t *info, const int32_t row)
{
	register int32_t ix;
	const int32_t max_iter = info->iterations;
	const double dx = info->dx;
	const int32_t xsize = info->xsize;
	double xc = info->xmin, yc = info->ymin + ((double)row * info->dy);
	uint16_t *data = info->data;

	/* Even numbers of columns */
	for (ix = 0; LIKELY(ix < (xsize & (int32_t)0xfffffffe)); ix += 2) {
		register double x0 = 0.0, y0 = 0.0;
		register double x1 = 0.0, y1 = 0.0;
		register int32_t iter0 = 0;
		register int32_t iter1 = 0;
		register double xc1 = xc + dx;

		for (;;) {
			register double x0_2, y0_2, x1_2, y1_2, t0, t1;
			register bool end0, end1;

			end0 = (iter0 >= max_iter);
			end1 = (iter1 >= max_iter);
			if (UNLIKELY(end0 & end1))
				break;

			x0_2 = x0 * x0;
			y0_2 = y0 * y0;
			end0 |= (x0_2 + y0_2 >= 4.0);
			x1_2 = x1 * x1;
			y1_2 = y1 * y1;
			end1 |= (x1_2 + y1_2 >= 4.0);
			iter0 += !end0;
			iter1 += !end1;

			if (end0 & end1)
				break;

			t0 = x0_2 - y0_2 + xc;
			t1 = x1_2 - y1_2 + xc1;
			y0 = (2 * x0 * y0) + yc;
			y1 = (2 * x1 * y1) + yc;
			x0 = t0;
			x1 = t1;
		}
		xc = xc1 + dx;
		data[0] = (uint16_t)iter0;
		data[1] = (uint16_t)iter1;
		data += 2;
	}

	/* residual */
	for (; LIKELY(ix < xsize); ix++) {
		register double x = 0.0, y = 0.0;
		register int32_t iter = 0;

		while (LIKELY(iter < max_iter)) {
			register const double x2 = x * x;
			register const double y2 = y * y;
			register double t;

			if (x2 + y2 >= 4.0)
				break;

			t = x2 - y2 + xc;
			xc += dx;
			iter++;
			y = (2 * x * y) + yc;
			x = t;
		}
		*(data++) = (uint16_t)iter;
	}
}

/*
 *  stress_fractal_julia()
 *	classic Julia set generator, naive method, unrolled x 2
 */
static void OPTIMIZE3 TARGET_CLONES stress_fractal_julia(fractal_info_t *info, const int32_t row)
{
	register int32_t ix;
	const int32_t max_iter = info->iterations;
	const double y_start = info->ymin + ((double)row * info->dy);
	const double dx = info->dx;
	const int32_t xsize = info->xsize;
	double x_start = info->xmin;
	uint16_t *data = info->data;

	/* Even numbers of columns */
	for (ix = 0; LIKELY(ix < (xsize & (int32_t)0xfffffffe)); ix += 2) {
		register int32_t iter0 = 0;
		register int32_t iter1 = 0;
		register double x0 = x_start;
		register double y0 = y_start;
		register double x1 = x_start + dx;
		register double y1 = y_start;

		for (;;) {
			register double x0_2, y0_2, x1_2, y1_2, t0, t1;
			register bool end0, end1;

			end0 = (iter0 >= max_iter);
			end1 = (iter1 >= max_iter);
			if (UNLIKELY(end0 & end1))
				break;

			x0_2 = x0 * x0;
			y0_2 = y0 * y0;
			end0 |= (x0_2 + y0_2 >= 4.0);
			x1_2 = x1 * x1;
			y1_2 = y1 * y1;
			end1 |= (x1_2 + y1_2 >= 4.0);
			iter0 += !end0;
			iter1 += !end1;

			if (end0 & end1)
				break;

			t0 = x0_2 - y0_2 - 0.79;
			t1 = x1_2 - y1_2 - 0.79;
			y0 = (2 * x0 * y0) + 0.15;
			y1 = (2 * x1 * y1) + 0.15;
			x0 = t0;
			x1 = t1;
		}
		x_start += dx + dx;
		data[0] = (uint16_t)iter0;
		data[1] = (uint16_t)iter1;
		data += 2;
	}

	/* residual */
	for (; ix < xsize; ix++) {
		register int32_t iter = 0;
		register double x = x_start;
		register double y = y_start;

		while (LIKELY(iter < max_iter)) {
			register const double x2 = x * x;
			register const double y2 = y * y;
			register double t;

			if (x2 + y2 > 4.0)
				break;

			t = x2 - y2 - 0.79;
			iter++;
			y = (2 * x * y) + 0.15;
			x = t;
		}
		x_start += dx;
		*(data++) = (uint16_t)iter;
	}
}

static const stress_fractal_method_t stress_fractal_methods[] = {
	{ "mandelbrot",	stress_fractal_mandelbrot,
		{ -2.0, 0.47, -1.15, 1.15, 0.0, 0.0, NULL, 1024, 1024, 256 } },
	{ "julia",	stress_fractal_julia,
		{ -1.5, 1.5, -1.0, 1.0, 0.0, 0.0, NULL, 1024, 1024, 256 } },
};

static const char *stress_fractal_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_fractal_methods)) ? stress_fractal_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_fractal_iterations, "fractal-iterations", TYPE_ID_INT32, MIN_FRACTAL_ITERATIONS, MAX_FRACTAL_ITERATIONS, NULL },
	{ OPT_fractal_method,     "fractal-method",     TYPE_ID_SIZE_T_METHOD, 0, 0, stress_fractal_method },
	{ OPT_fractal_xsize,      "fractal-xsize",      TYPE_ID_INT32, MIN_FRACTAL_XSIZE, MAX_FRACTAL_XSIZE, NULL },
	{ OPT_fractal_ysize,      "fractal-ysize",      TYPE_ID_INT32, MIN_FRACTAL_YSIZE, MAX_FRACTAL_YSIZE, NULL },
	END_OPT,
};

static int stress_fractal(stress_args_t *args)
{
	fractal_info_t info;
	fractal_func func;
	size_t fractal_method = 0;	/* mandelbrot */
	size_t data_sz;
	double rate, rows = 0.0, t, duration = 0.0;

	(void)stress_get_setting("fractal-method", &fractal_method);

	info = stress_fractal_methods[fractal_method].info;
	func = stress_fractal_methods[fractal_method].func;

	if (!stress_get_setting("fractal-iterations", &info.iterations)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			info.iterations = MAX_FRACTAL_ITERATIONS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			info.iterations = MIN_FRACTAL_ITERATIONS;
	}
	if (!stress_get_setting("fractal-xsize", &info.xsize)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			info.xsize = MAX_FRACTAL_XSIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			info.xsize = MIN_FRACTAL_XSIZE;
	}
	if (!stress_get_setting("fractal-ysize", &info.ysize)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			info.ysize = MAX_FRACTAL_XSIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			info.ysize = MIN_FRACTAL_XSIZE;
	}

	data_sz = sizeof(*info.data) * (size_t)info.xsize;
	info.data = stress_mmap_populate(NULL, data_sz, PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (info.data == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap fractal data buffer of %zu bytes%s, skipping stressor\n",
			args->name, data_sz, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(info.data, data_sz, "fractal-data");
	if (stress_instance_zero(args)) {
		pr_inf("%s: %s, %" PRId32 " x %" PRId32 ", %" PRId32 " iterations, "
			"(%.2f, %.2fi) .. (%.2f, %.2fi)\n",
			args->name, stress_fractal_methods[fractal_method].name,
			info.xsize, info.ysize, info.iterations,
			info.xmin, info.ymin, info.xmax, info.ymax);
	}

	if (!g_shared->fractal.lock) {
		pr_inf_skip("%s: failed to create shared fractal row lock, skipping stressor\n",
			args->name);
		(void)munmap((void *)info.data, data_sz);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	info.dx = (info.xmax - info.xmin) / (double)info.xsize;
	info.dy = (info.ymax - info.ymin) / (double)info.ysize;

	t = stress_time_now();
	do {
		register const int32_t row = stress_fractal_get_row(args, info.ysize);

		func(&info, row);
		rows++;
	} while (stress_continue(args));
	duration = stress_time_now() - t;

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (duration > 0.0) ? (rows * (double)info.xsize) / duration : 0.0;
	stress_metrics_set(args, 0, "points per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

	rate = (duration > 0.0) ? (rows / (double)info.ysize) / duration : 0.0;
	stress_metrics_set(args, 1, "fractals per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

	(void)munmap((void *)info.data, data_sz);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_fractal_info = {
	.stressor = stress_fractal,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.init = stress_fractal_init,
	.deinit = stress_fractal_deinit,
	.verify = VERIFY_NONE,
	.opts = opts,
	.help = help
};
