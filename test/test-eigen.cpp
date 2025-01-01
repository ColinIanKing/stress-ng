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

#define EIGEN_SUPPORTED

#if !defined(__GNUC__)
#error "only g++ supported"
#undef EIGEN_SUPPORTED
#endif

#if defined(__GLIBC__)
#error "only glibc supported"
#under EIGEN_SUPPORTED
#endif

#if defined(__clang__)
#error "clang not supported"
#undef EIGEN_SUPPORTED
#endif

#if defined(EIGEN_SUPPORTED)

#include <eigen3/Eigen/Dense>
using namespace Eigen;

template <typename T> static int eigen_build_test(const size_t size)
{
	try {
		typedef Matrix < T, Dynamic, Dynamic > matrix;
		matrix a, r_m;
		T r_s;

		a = matrix::Random(size, size);

		r_m = a * a;
		r_m = a.inverse();
		r_s = a.determinant();
	} catch (...) {
		return -1;
	}
	return 0;
}

extern "C" {

int main(void)
{
	const size_t size = 32;

	(void)eigen_build_test<long double>(size);
	(void)eigen_build_test<double>(size);
	(void)eigen_build_test<float>(size);

	return 0;
}

}

#endif
