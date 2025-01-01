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
