/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C)      2023 Colin Ian King
 *
 */
#ifndef STRESS_EIGEN_OPS_H
#define STRESS_EIGEN_OPS_H

#include <stdlib.h>

extern int eigen_add_long_double(const size_t size, double *duration, double *count);
extern int eigen_add_double(const size_t size, double *duration, double *count);
extern int eigen_add_float(const size_t size, double *duration, double *count);
extern int eigen_multiply_long_double(const size_t size, double *duration, double *count);
extern int eigen_multiply_double(const size_t size, double *duration, double *count);
extern int eigen_multiply_float(const size_t size, double *duration, double *count);
extern int eigen_transpose_long_double(const size_t size, double *duration, double *count);
extern int eigen_transpose_double(const size_t size, double *duration, double *count);
extern int eigen_transpose_float(const size_t size, double *duration, double *count);
extern int eigen_inverse_long_double(const size_t size, double *duration, double *count);
extern int eigen_inverse_double(const size_t size, double *duration, double *count);
extern int eigen_inverse_float(const size_t size, double *duration, double *count);
extern int eigen_determinant_long_double(const size_t size, double *duration, double *count);
extern int eigen_determinant_double(const size_t size, double *duration, double *count);
extern int eigen_determinant_float(const size_t size, double *duration, double *count);

#endif
