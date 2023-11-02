/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C)      2023 Colin Ian King
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
