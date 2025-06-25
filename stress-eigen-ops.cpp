/*
 * Copyright (C) 2023-2025 Colin Ian King
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
extern "C" {
#include "config.h"
#include "stress-eigen-ops.h"

extern double stress_time_now(void);
}

#if defined(HAVE_EIGEN)

#define THRESHOLD	(0.0001)

#include <eigen3/Eigen/Dense>
using namespace Eigen;

template <typename T> static int eigen_add(const size_t size, double *duration, double *count)
{
	try {
		typedef Matrix < T, Dynamic, Dynamic > matrix;
		matrix a, b, result, result_check;
		double t;
		bool r;

		a = matrix::Random(size, size);
		b = matrix::Random(size, size);

		t = stress_time_now();
		result = a + b;
		*duration += stress_time_now() - t;
		*count += 1.0;

		t = stress_time_now();
		result_check = a + b;
		*duration += stress_time_now() - t;
		*count += 1.0;

		r = ((result_check - result).norm() < THRESHOLD);
		if (!r)
			return EXIT_FAILURE;
	} catch (...) {
		return -1;
	}
	return EXIT_SUCCESS;
}

template <typename T> static int eigen_multiply(const size_t size, double *duration, double *count)
{
	try {
		typedef Matrix < T, Dynamic, Dynamic > matrix;
		matrix a, b, result, result_check;
		double t;
		bool r;

		a = matrix::Random(size, size);
		b = matrix::Random(size, size);

		t = stress_time_now();
		result = a * b;
		*duration += stress_time_now() - t;
		*count += 1.0;

		t = stress_time_now();
		result_check = a * b;
		*duration += stress_time_now() - t;
		*count += 1.0;

		r = ((result_check - result).norm() < THRESHOLD);
		if (!r)
			return EXIT_FAILURE;
	} catch (...) {
		return -1;
	}
	return EXIT_SUCCESS;
}

template <typename T> static int eigen_transpose(const size_t size, double *duration, double *count)
{
	try {
		typedef Matrix < T, Dynamic, Dynamic > matrix;
		matrix a, result, result_check;
		double t;
		bool r;

		a = matrix::Random(size, size);

		t = stress_time_now();
		result = a.transpose();
		*duration += stress_time_now() - t;
		*count += 1.0;

		t = stress_time_now();
		result_check = a.transpose();
		*duration += stress_time_now() - t;
		*count += 1.0;

		r = ((result_check - result).norm() < THRESHOLD);
		if (!r)
			return EXIT_FAILURE;
	} catch (...) {
		return -1;
	}
	return EXIT_SUCCESS;
}

template <typename T> static int eigen_inverse(const size_t size, double *duration, double *count)
{
	try {
		typedef Matrix < T, Dynamic, Dynamic > matrix;
		matrix a, result, result_check;
		double t;
		bool r;

		a = matrix::Random(size, size);

		t = stress_time_now();
		result = a.inverse();
		*duration += stress_time_now() - t;
		*count += 1.0;

		t = stress_time_now();
		result_check = a.inverse();
		*duration += stress_time_now() - t;
		*count += 1.0;

		r = ((result_check - result).norm() < THRESHOLD);
		if (!r)
			return EXIT_FAILURE;
	} catch (...) {
		return -1;
	}
	return EXIT_SUCCESS;
}

template <typename T> static int eigen_determinant(const size_t size, double *duration, double *count)
{

	try {
		typedef Matrix < T, Dynamic, Dynamic > matrix;
		matrix a;
		T result, result_check;
		double t;

		a = matrix::Random(size, size);

		t = stress_time_now();
		result = a.determinant();
		*duration += stress_time_now() - t;
		*count += 1.0;

		t = stress_time_now();
		result_check = a.determinant();
		*duration += stress_time_now() - t;
		*count += 1.0;

		if ((result_check - result) > 0.0001)
			return EXIT_FAILURE;
	} catch (...) {
		return -1;
	}
	return EXIT_SUCCESS;
}

extern "C" {

int eigen_add_long_double(const size_t size, double *duration, double *count)
{
	return eigen_add<long double>(size, duration, count);
}

int eigen_add_double(const size_t size, double *duration, double *count)
{
	return eigen_add<double>(size, duration, count);
}

int eigen_add_float(const size_t size, double *duration, double *count)
{
	return eigen_add<float>(size, duration, count);
}

int eigen_multiply_long_double(const size_t size, double *duration, double *count)
{
	return eigen_multiply<long double>(size, duration, count);
}

int eigen_multiply_double(const size_t size, double *duration, double *count)
{
	return eigen_multiply<double>(size, duration, count);
}

int eigen_multiply_float(const size_t size, double *duration, double *count)
{
	return eigen_multiply<float>(size, duration, count);
}

int eigen_transpose_long_double(const size_t size, double *duration, double *count)
{
	return eigen_transpose<long double>(size, duration, count);
}

int eigen_transpose_double(const size_t size, double *duration, double *count)
{
	return eigen_transpose<double>(size, duration, count);
}

int eigen_transpose_float(const size_t size, double *duration, double *count)
{
	return eigen_transpose<float>(size, duration, count);
}

int eigen_inverse_long_double(const size_t size, double *duration, double *count)
{
	return eigen_inverse<long double>(size, duration, count);
}

int eigen_inverse_double(const size_t size, double *duration, double *count)
{
	return eigen_inverse<double>(size, duration, count);
}

int eigen_inverse_float(const size_t size, double *duration, double *count)
{
	return eigen_inverse<float>(size, duration, count);
}

int eigen_determinant_long_double(const size_t size, double *duration, double *count)
{
	return eigen_determinant<long double>(size, duration, count);
}

int eigen_determinant_double(const size_t size, double *duration, double *count)
{
	return eigen_determinant<double>(size, duration, count);
}

int eigen_determinant_float(const size_t size, double *duration, double *count)
{
	return eigen_determinant<float>(size, duration, count);
}

}

#endif
