/*
 * Copyright (C) 2022-2025 Colin Ian King
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
#ifndef CORE_BUILTIN_H
#define CORE_BUILTIN_H

#include "core-attribute.h"

#if defined(HAVE_X86INTRIN_H)
#include <x86intrin.h>
#endif

#if defined(HAVE_BUILTIN_ASSUME_ALIGNED)
#define shim_assume_aligned(arg, n)	__builtin_assume_aligned((arg), (n))
#else
#define shim_assume_aligned(arg, n)	arg
#endif

#if defined(HAVE_BUILTIN_MEMSET)
#define shim_memset(s, c, n)		__builtin_memset((s), (c), (n))
#else
#define shim_memset(s, c, n)		memset((s), (c), (n))
#endif

#if defined(HAVE_BUILTIN_MEMCPY)
#define	shim_memcpy(dst, src, n)	__builtin_memcpy((dst), (src), (n))
#else
#define	shim_memcpy(dst, src, n)	memcpy((dst), (src), (n))
#endif

#if defined(HAVE_BUILTIN_MEMMOVE)
#define	shim_memmove(dst, src, n)	__builtin_memmove((dst), (src), (n))
#else
#define	shim_memmove(dst, src, n)	memmove((dst), (src), (n))
#endif

#if defined(HAVE_BUILTIN_MEMCMP)
#define	shim_memcmp(dst, src, n)	__builtin_memcmp((dst), (src), (n))
#else
#define	shim_memcmp(dst, src, n)	memcmp((dst), (src), (n))
#endif

#if defined(HAVE_BUILTIN_STRDUP)
#define	shim_strdup(str)		__builtin_strdup((str))
#else
#define	shim_strdup(str)		strdup((str))
#endif

#if defined(HAVE_BUILTIN_CABS)
#define shim_cabs(x)		__builtin_cabs((x))
#else
#define shim_cabs(x)		cabs((x))
#endif

#if defined(HAVE_BUILTIN_CABSF)
#define shim_cabsf(x)		__builtin_cabsf((x))
#else
#define shim_cabsf(x)		cabsf((x))
#endif

#if defined(HAVE_BUILTIN_CABSL)
#define shim_cabsl(x)		__builtin_cabsl((x))
#else
#if defined(HAVE_CABSL)
#define shim_cabsl(x)		cabsl((x))
#else
#define shim_cabsl(x)		((long double)cabs((double complex)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_LGAMMAL)
#define shim_lgammal(x)		__builtin_lgammal((x))
#else
#if defined(HAVE_LGAMMAL)
#define shim_lgammal(x)		lgammal((x))
#else
#define shim_lgammal(x)		((long double)lgamma((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_CPOW)
#define shim_cpow(x, z)		__builtin_cpow((x), (z))
#else
#if defined(HAVE_CPOW)
#define shim_cpow(x, z)		cpow((x), (z))
#else
#define shim_cpow(x, z)		(shim_cexp((z) * shim_clog((x))))
#endif
#endif

#if defined(HAVE_BUILTIN_CPOWF)
#define shim_cpowf(x, z)	__builtin_cpowf((x), (z))
#else
#if defined(HAVE_CPOWF)
#define shim_cpowf(x, z)	cpowf((x), (z))
#else
#define shim_cpowf(x, z)	(shim_cexpf(z) * shim_clogf((x)))
#endif
#endif

#if defined(HAVE_BUILTIN_CPOWL)
#define shim_cpowl(x, z)	__builtin_cpowl((x), (z))
#else
#if defined(HAVE_CPOWL)
#define shim_cpowl(x, z)	cpowl((x), (z))
#else
#define shim_cpowl(x, z)	(shim_cexpl((z) * shim_clogl((x))))
#endif
#endif

#if defined(HAVE_BUILTIN_HYPOT)
#define shim_hypot(x, y)	__builtin_hypot((x), (y))
#else
#define shim_hypot(x, y)	hypot((x), (y))
#endif

#if defined(HAVE_BUILTIN_HYPOTF)
#define shim_hypotf(x, y)	__builtin_hypotf((x), (y))
#else
#define shim_hypotf(x, y)	hypotf((x), (y))
#endif

#if defined(HAVE_BUILTIN_HYPOTL)
#define shim_hypotl(x, y)	__builtin_hypotl((x), (y))
#else
#if defined(HAVE_HYPOTL)
#define shim_hypotl(x, y)	hypotl((x), (y))
#else
#define shim_hypotl(x, y)	((long double)hypot((double)(x), (double)(y)))
#endif
#endif

#if defined(HAVE_BUILTIN_POW)
#define shim_pow(x, y)		__builtin_pow((x), (y))
#else
#define shim_pow(x, y)		pow((x), (y))
#endif

#if defined(HAVE_BUILTIN_POWF)
#define shim_powf(x, y)		__builtin_powf((x), (y))
#else
#define shim_powf(x, y)		powf((x), (y))
#endif

#if defined(HAVE_BUILTIN_POWL)
#define shim_powl(x, y)		__builtin_powl((x), (y))
#else
#if defined(HAVE_POWL)
#define shim_powl(x, y)		powl((x), (y))
#else
#define shim_powl(x, y)		((long double)pow((double)(x), (double)(y)))
#endif
#endif

#if defined(HAVE_BUILTIN_RINTL)
#define shim_rintl(x)		__builtin_rintl((x))
#else
#if defined(HAVE_RINTL)
#define shim_rintl(x)		rintl((x))
#else
#define shim_rintl(x)		((long double)shim_rint((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_CLOGF)
#define shim_clogf(x)		__builtin_clogf((x))
#else
#define shim_clogf(x)		clogf((x))
#endif

#if defined(HAVE_BUILTIN_CLOG)
#define shim_clog(x)		__builtin_clog((x))
#else
#define shim_clog(x)		clog((x))
#endif

#if defined(HAVE_BUILTIN_CLOGL)
#define shim_clogl(x)		__builtin_clogl((x))
#else
#if defined(HAVE_CLOGL)
#define shim_clogl(x)		clogl((x))
#else
#define shim_clogl(x)		((long double complex)shim_clog((double complex)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_LOGF)
#define shim_logf(x)		__builtin_logf((x))
#else
#define shim_logf(x)		logf((x))
#endif

#if defined(HAVE_BUILTIN_LOG)
#define shim_log(x)		__builtin_log((x))
#else
#define shim_log(x)		log((x))
#endif

#if defined(HAVE_BUILTIN_LOGL)
#define shim_logl(x)		__builtin_logl((x))
#else
#if defined(HAVE_LOGL)
#define shim_logl(x)		logl((x))
#else
#define shim_logl(x)		((long double)shim_log((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_LOGBF)
#define shim_logbf(x)		__builtin_logbf((x))
#else
#define shim_logbf(x)		logbf((x))
#endif

#if defined(HAVE_BUILTIN_LOGB)
#define shim_logb(x)		__builtin_logb((x))
#else
#define shim_logb(x)		logb((x))
#endif

#if defined(HAVE_BUILTIN_LOGBL)
#define shim_logbl(x)		__builtin_logbl((x))
#else
#if defined(HAVE_LOGBL)
#define shim_logbl(x)		logbl((x))
#else
#define shim_logbl(x)		((long double)shim_logb((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_LOG10F)
#define shim_log10f(x)		__builtin_log10f((x))
#else
#define shim_log10f(x)		log10f((x))
#endif

#if defined(HAVE_BUILTIN_LOG10)
#define shim_log10(x)		__builtin_log10((x))
#else
#define shim_log10(x)		log10((x))
#endif

#if defined(HAVE_BUILTIN_LOG10L)
#define shim_log10l(x)		__builtin_log10l((x))
#else
#if defined(HAVE_LOG10L)
#define shim_log10l(x)		log10l((x))
#else
#define shim_log10l(x)		((long double)shim_log10((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_LOG2F)
#define shim_log2f(x)		__builtin_log2f((x))
#else
#define shim_log2f(x)		log2f((x))
#endif

#if defined(HAVE_BUILTIN_LOG2)
#define shim_log2(x)		__builtin_log2((x))
#else
#define shim_log2(x)		log2((x))
#endif

#if defined(HAVE_BUILTIN_LOG2L)
#define shim_log2l(x)		__builtin_log2l((x))
#else
#if defined(HAVE_LOG2L)
#define shim_log2l(x)		log2l((x))
#else
#define shim_log2l(x)		((long double)shim_log2((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_EXP)
#define shim_exp(x)		__builtin_exp((x))
#else
#define shim_exp(x)		exp((x))
#endif

#if defined(HAVE_BUILTIN_EXPF)
#define shim_expf(x)		__builtin_expf((x))
#else
#define shim_expf(x)		expf((x))
#endif

#if defined(HAVE_BUILTIN_EXPL)
#define shim_expl(x)		__builtin_expl((x))
#else
#if defined(HAVE_EXPL) && !defined(__HAIKU__)
#define shim_expl(x)		expl((x))
#else
#define shim_expl(x)		((long double)shim_exp((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_EXP2)
#define shim_exp2(x)		__builtin_exp2((x))
#else
#define shim_exp2(x)		exp2((x))
#endif

#if defined(HAVE_BUILTIN_EXP2F)
#define shim_exp2f(x)		__builtin_exp2f((x))
#else
#define shim_exp2f(x)		exp2f((x))
#endif

#if defined(HAVE_BUILTIN_EXP2L)
#define shim_exp2l(x)		__builtin_exp2l((x))
#else
#if defined(HAVE_EXP2L) && !defined(__HAIKU__)
#define shim_exp2l(x)		exp2l((x))
#else
#define shim_exp2l(x)		((long double)shim_exp2((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_EXP10)
#define shim_exp10(x)		__builtin_exp10((x))
#else
#define shim_exp10(x)		exp10((x))
#endif

#if defined(HAVE_BUILTIN_EXP10F)
#define shim_exp10f(x)		__builtin_exp10f((x))
#else
#define shim_exp10f(x)		exp10f((x))
#endif

#if defined(HAVE_BUILTIN_EXP10L)
#define shim_exp10l(x)		__builtin_exp10l((x))
#else
#if defined(HAVE_EXP10L) && !defined(__HAIKU__)
#define shim_exp10l(x)		exp10l((x))
#else
#define shim_exp10l(x)		((long double)shim_exp10((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_FMA)
#define shim_fma(x, y, z)	__builtin_fma((x), (y), (z))
#else
#define shim_fma(x, y, z)	fma((x), (y), (z))
#endif

#if defined(HAVE_BUILTIN_FMAF)
#define shim_fmaf(x, y, z)	__builtin_fmaf((x), (y), (z))
#else
#define shim_fmaf(x, y, z)	fmaf((x), (y), (z))
#endif

#if defined(HAVE_BUILTIN_CBRT)
#define shim_cbrt(x)		__builtin_cbrt((x))
#else
#define shim_cbrt(x)		cbrt((x))
#endif

#if defined(HAVE_BUILTIN_CBRTF)
#define shim_cbrtf(x)		__builtin_cbrtf((x))
#else
#define shim_cbrtf(x)		cbrtf((x))
#endif

#if defined(HAVE_BUILTIN_CBRTL)
#define shim_cbrtl(x)		__builtin_cbrtl((x))
#else
#if defined(HAVE_CSQRTL)
#define shim_cbrtl(x)		cbrtl((x))
#else
#define shim_cbrtl(x)		((long double)shim_cbrt((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_CEXP)
#define shim_cexp(x)		__builtin_cexp((x))
#else
#define shim_cexp(x)		cexp((x))
#endif

#if defined(HAVE_BUILTIN_CEXPF)
#define shim_cexpf(x)		__builtin_cexpf((x))
#else
#define shim_cexpf(x)		cexpf((x))
#endif

#if defined(HAVE_BUILTIN_CEXPL)
#define shim_cexpl(x)		__builtin_cexpl((x))
#else
#if defined(HAVE_CEXPL)
#define shim_cexpl(x)		cexpl((x))
#else
#define	shim_cexpl(x)		((long double complex)cexp((double complex)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_COSF)
#define shim_cosf(x)		__builtin_cosf((x))
#else
#define shim_cosf(x)		cosf((x))
#endif

#if defined(HAVE_BUILTIN_COS)
#define shim_cos(x)		__builtin_cos((x))
#else
#define shim_cos(x)		cos((x))
#endif

#if defined(HAVE_BUILTIN_COSL)
#define shim_cosl(x)		__builtin_cosl((x))
#else
#if defined(HAVE_COSL)
#define shim_cosl(x)		cosl((x))
#else
#define shim_cosl(x)		((long double)shim_cos((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_COSHF)
#define shim_coshf(x)		__builtin_coshf((x))
#else
#define shim_coshf(x)		coshf((x))
#endif

#if defined(HAVE_BUILTIN_COSH)
#define shim_cosh(x)		__builtin_cosh((x))
#else
#define shim_cosh(x)		cosh((x))
#endif

#if defined(HAVE_BUILTIN_COSHL)
#define shim_coshl(x)		__builtin_coshl((x))
#else
#if defined(HAVE_COSHL)
#define shim_coshl(x)		coshl((x))
#else
#define shim_coshl(x)		((long double)cosh((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_CCOSHF)
#define shim_ccoshf(x)		__builtin_ccoshf((x))
#else
#define shim_ccoshf(x)		ccoshf((x))
#endif

#if defined(HAVE_BUILTIN_CCOSH)
#define shim_ccosh(x)		__builtin_ccosh((x))
#else
#define shim_ccosh(x)		ccosh((x))
#endif

#if defined(HAVE_BUILTIN_CCOSHL)
#define shim_ccoshl(x)		__builtin_ccoshl((x))
#else
#if defined(HAVE_CCOSHL)
#define shim_ccoshl(x)		ccoshl((x))
#else
#define shim_ccoshl(x)		((complex long double)ccosh((complex double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_CCOS)
#define shim_ccos(x)		__builtin_ccos((x))
#else
#if defined(HAVE_CCOS)
#define	shim_ccos(x)		ccos((x))
#else
#define	shim_ccos(x)		((shim_exp(I * (x)) + shim_exp(-I * (x))) / 2.0)
#endif
#endif

#if defined(HAVE_BUILTIN_CCOSF)
#define shim_ccosf(x)		__builtin_ccosf((x))
#else
#if defined(HAVE_CCOSF)
#define	shim_ccosf(x)		ccosf((x))
#else
#define	shim_ccosf(x)		((shim_expf(I * (x)) + shim_expf(-I * (x))) / 2.0)
#endif
#endif

#if defined(HAVE_BUILTIN_CCOSL)
#define shim_ccosl(x)		__builtin_ccosl((x))
#else
#if defined(HAVE_CCOSL)
#define	shim_ccosl(x)		ccosl((x))
#else
#define	shim_ccosl(x)		((shim_expl(I * (x)) + shim_expl(-I * (x))) / 2.0)
#endif
#endif

#if defined(HAVE_BUILTIN_CSQRT)
#define shim_csqrt(x)		__builtin_csqrt((x))
#else
#define shim_csqrt(x)		csqrt((x))
#endif

#if defined(HAVE_BUILTIN_CSQRTF)
#define shim_csqrtf(x)		__builtin_csqrtf((x))
#else
#define shim_csqrtf(x)		csqrtf((x))
#endif

#if defined(HAVE_BUILTIN_CSQRTL)
#define shim_csqrtl(x)		__builtin_csqrtl((x))
#else
#if defined(HAVE_CSQRTL)
#define shim_csqrtl(x)		csqrtl((x))
#else
#define shim_csqrtl(x)		((long double)shim_csqrt((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_SINF)
#define shim_sinf(x)		__builtin_sinf((x))
#else
#define shim_sinf(x)		sinf((x))
#endif

#if defined(HAVE_BUILTIN_SIN)
#define shim_sin(x)		__builtin_sin((x))
#else
#define shim_sin(x)		sin((x))
#endif

#if defined(HAVE_BUILTIN_SINL)
#define shim_sinl(x)		__builtin_sinl((x))
#else
#if defined(HAVE_SINL)
#define shim_sinl(x)		sinl((x))
#else
#define shim_sinl(x)		((long double)shim_sin((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_SINCOSF)
#define shim_sincosf(x, s, c)	__builtin_sincosf((x), (s), (c))
#else
#define shim_sincosf(x, s, c)	sincosf((x), (s), (c))
#endif

#if defined(HAVE_BUILTIN_SINCOS)
#define shim_sincos(x, s, c)	__builtin_sincos((x), (s), (c))
#else
#define shim_sincos(x, s, c)	sincos((x), (s), (c))
#endif

#if defined(HAVE_BUILTIN_SINCOSL)
#define shim_sincosl(x, s, c)	__builtin_sincosl((x), (s), (c))
#else
#define shim_sincosl(x, s, c)	sincosl((x), (s), (c))
#endif

#if defined(HAVE_BUILTIN_SINHF)
#define shim_sinhf(x)		__builtin_sinhf((x))
#else
#define shim_sinhf(x)		sinhf((x))
#endif

#if defined(HAVE_BUILTIN_SINH)
#define shim_sinh(x)		__builtin_sinh((x))
#else
#define shim_sinh(x)		sinh((x))
#endif

#if defined(HAVE_BUILTIN_SINHL)
#define shim_sinhl(x)		__builtin_sinhl((x))
#else
#if defined(HAVE_SINHL)
#define shim_sinhl(x)		sinhl((x))
#else
#define shim_sinhl(x)		((long double)sinh((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_CSINHF)
#define shim_csinhf(x)		__builtin_csinhf((x))
#else
#define shim_csinhf(x)		csinhf((x))
#endif

#if defined(HAVE_BUILTIN_CSINH)
#define shim_csinh(x)		__builtin_csinh((x))
#else
#define shim_csinh(x)		csinh((x))
#endif

#if defined(HAVE_BUILTIN_CSINHL)
#define shim_csinhl(x)		__builtin_csinhl((x))
#else
#if defined(HAVE_CSINHL)
#define shim_csinhl(x)		csinhl((x))
#else
#define shim_csinhl(x)		((complex long double)csinh((complex double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_CSIN)
#define shim_csin(x)		__builtin_csin((x))
#else
#if defined(HAVE_CSIN)
#define	shim_csin(x)		csin((x))
#else
#define	shim_csin(x)		((shim_exp(I * (x)) - shim_exp(-I * (x))) / (2.0 * I))
#endif
#endif

#if defined(HAVE_BUILTIN_CSINF)
#define shim_csinf(x)		__builtin_csinf((x))
#else
#if defined(HAVE_CSINF)
#define	shim_csinf(x)		csinf((x))
#else
#define	shim_csinf(x)		((shim_expf(I * (x)) - shim_expf(-I * (x))) / (2.0 * I))
#endif
#endif

#if defined(HAVE_BUILTIN_CSINL)
#define shim_csinl(x)		__builtin_csinl((x))
#else
#if defined(HAVE_CSINL)
#define	shim_csinl(x)		csinl((x))
#else
#define	shim_csinl(x)		((shim_expl(I * (x)) - shim_expl(-I * (x))) / (2.0 * I))
#endif
#endif

#if defined(HAVE_BUILTIN_TANF)
#define shim_tanf(x)		__builtin_tanf((x))
#else
#define shim_tanf(x)		tanf((x))
#endif

#if defined(HAVE_BUILTIN_TAN)
#define shim_tan(x)		__builtin_tan((x))
#else
#define shim_tan(x)		tan((x))
#endif

#if defined(HAVE_BUILTIN_TANL)
#define shim_tanl(x)		__builtin_tanl((x))
#else
#if defined(HAVE_TANL)
#define shim_tanl(x)		tanl((x))
#else
#define shim_tanl(x)		((long double)shim_tan((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_TANHF)
#define shim_tanhf(x)		__builtin_tanhf((x))
#else
#define shim_tanhf(x)		tanhf((x))
#endif

#if defined(HAVE_BUILTIN_TANH)
#define shim_tanh(x)		__builtin_tanh((x))
#else
#define shim_tanh(x)		tanh((x))
#endif

#if defined(HAVE_BUILTIN_TANHL)
#define shim_tanhl(x)		__builtin_tanhl((x))
#else
#if defined(HAVE_TANHL)
#define shim_tanhl(x)		tanhl((x))
#else
#define	shim_tanhl(x)		((long double)shim_tanh((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_CTANF)
#define shim_ctanf(x)		__builtin_ctanf((x))
#else
#define shim_ctanf(x)		ctanf((x))
#endif

#if defined(HAVE_BUILTIN_CTAN)
#define shim_ctan(x)		__builtin_ctan((x))
#else
#define shim_ctan(x)		ctan((x))
#endif

#if defined(HAVE_BUILTIN_CTANL)
#define shim_ctanl(x)		__builtin_ctanl((x))
#else
#if defined(HAVE_CTANL)
#define shim_ctanl(x)		ctanl((x))
#else
#define shim_ctanl(x)		((long double)shim_ctan((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_CTANHF)
#define shim_ctanhf(x)		__builtin_ctanhf((x))
#else
#define shim_ctanhf(x)		ctanhf((x))
#endif

#if defined(HAVE_BUILTIN_CTANH)
#define shim_ctanh(x)		__builtin_ctanh((x))
#else
#define shim_ctanh(x)		ctanh((x))
#endif

#if defined(HAVE_BUILTIN_CTANHL)
#define shim_ctanhl(x)		__builtin_ctanhl((x))
#else
#if defined(HAVE_CTANHL)
#define shim_ctanhl(x)		ctanhl((x))
#else
#define shim_ctanhl(x)		((complex long double)shim_ctanh((complex double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_SQRT)
#define shim_sqrt(x)		__builtin_sqrt((x))
#else
#define shim_sqrt(x)		sqrt((x))
#endif

#if defined(HAVE_BUILTIN_SQRTF)
#define shim_sqrtf(x)		__builtin_sqrtf((x))
#else
#define shim_sqrtf(x)		sqrtf((x))
#endif

#if defined(HAVE_BUILTIN_SQRTL)
#define shim_sqrtl(x)		__builtin_sqrtl((x))
#else
#if defined(HAVE_SQRTL)
#define shim_sqrtl(x)		sqrtl((x))
#else
#define shim_sqrtl(x)		((long double)shim_sqrt((double)x))
#endif
#endif

#if defined(HAVE_BUILTIN_FABS)
#define shim_fabs(x)		__builtin_fabs((x))
#else
#define shim_fabs(x)		fabs((x))
#endif

#if defined(HAVE_BUILTIN_FABSF)
#define shim_fabsf(x)		__builtin_fabsf((x))
#else
#define shim_fabsf(x)		fabsf((x))
#endif

#if defined(HAVE_BUILTIN_FABSL)
#define shim_fabsl(x)		__builtin_fabsl((x))
#else
#define shim_fabsl(x)		fabsl((x))
#endif

#if defined(HAVE_BUILTIN_LLABS)
#define shim_llabs(x)		__builtin_llabs((x))
#else
#define shim_llabs(x)		llabs((x))
#endif

#if defined(HAVE_BUILTIN_J0)
#define shim_j0(x)		__builtin_j0((x))
#else
#define shim_j0(x)		j0((x))
#endif

#if defined(HAVE_BUILTIN_J0F)
#define shim_j0f(x)		__builtin_j0f((x))
#else
#define shim_j0f(x)		j0f((x))
#endif

#if defined(HAVE_BUILTIN_J0L)
#define shim_j0l(x)		__builtin_j0l((x))
#else
#define shim_j0l(x)		j0l((x))
#endif

#if defined(HAVE_BUILTIN_J1)
#define shim_j1(x)		__builtin_j1((x))
#else
#define shim_j1(x)		j1((x))
#endif

#if defined(HAVE_BUILTIN_J1F)
#define shim_j1f(x)		__builtin_j1f((x))
#else
#define shim_j1f(x)		j1f((x))
#endif

#if defined(HAVE_BUILTIN_J1L)
#define shim_j1l(x)		__builtin_j1l((x))
#else
#define shim_j1l(x)		j1l((x))
#endif

#if defined(HAVE_BUILTIN_JN)
#define shim_jn(n, x)		__builtin_jn((n), (x))
#else
#define shim_jn(n, x)		jn((n), (x))
#endif

#if defined(HAVE_BUILTIN_JNF)
#define shim_jnf(n, x)		__builtin_jnf((n), (x))
#else
#define shim_jnf(n, x)		jnf((n), (x))
#endif

#if defined(HAVE_BUILTIN_JNL)
#define shim_jnl(n, x)		__builtin_jnl((n), (x))
#else
#define shim_jnl(n, x)		jnl((n), (x))
#endif

#if defined(HAVE_BUILTIN_RINT)
#define shim_rint(x)		__builtin_rint((x))
#else
#define shim_rint(x)		rint((x))
#endif

#if defined(HAVE_BUILTIN_Y0)
#define shim_y0(x)		__builtin_y0((x))
#else
#define shim_y0(x)		y0((x))
#endif

#if defined(HAVE_BUILTIN_Y0F)
#define shim_y0f(x)		__builtin_y0f((x))
#else
#define shim_y0f(x)		y0f((x))
#endif

#if defined(HAVE_BUILTIN_Y0L)
#define shim_y0l(x)		__builtin_y0l((x))
#else
#define shim_y0l(x)		y0l((x))
#endif

#if defined(HAVE_BUILTIN_Y1)
#define shim_y1(x)		__builtin_y1((x))
#else
#define shim_y1(x)		y1((x))
#endif

#if defined(HAVE_BUILTIN_Y1F)
#define shim_y1f(x)		__builtin_y1f((x))
#else
#define shim_y1f(x)		y1f((x))
#endif

#if defined(HAVE_BUILTIN_Y1L)
#define shim_y1l(x)		__builtin_y1l((x))
#else
#define shim_y1l(x)		y1l((x))
#endif

#if defined(HAVE_BUILTIN_YN)
#define shim_yn(n, x)		__builtin_yn((n), (x))
#else
#define shim_yn(n, x)		yn((n), (x))
#endif

#if defined(HAVE_BUILTIN_YNF)
#define shim_ynf(n, x)		__builtin_ynf((n), (x))
#else
#define shim_ynf(n, x)		ynf((n), (x))
#endif

#if defined(HAVE_BUILTIN_YNL)
#define shim_ynl(n, x)		__builtin_ynl((n), (x))
#else
#define shim_ynl(n, x)		ynl((n), (x))
#endif

#if defined(HAVE_BUILTIN_ROUND)
#define shim_round(x)		__builtin_round((x))
#else
#define shim_round(x)		round((x))
#endif

#if defined(HAVE_BUILTIN_ROUNDL)
#define shim_roundl(x)		__builtin_roundl((x))
#else
#if defined(HAVE_ROUNDL)
#define shim_roundl(x)		roundl((x))
#else
#define shim_roundl(x)		((long double)shim_round((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_STDC_ROTATE_LEFT)
#define shim_rol8n(x, bits)	 __builtin_stdc_rotate_left(x, bits)
#define shim_rol16n(x, bits)	 __builtin_stdc_rotate_left(x, bits)
#define shim_rol32n(x, bits)	 __builtin_stdc_rotate_left(x, bits)
#define shim_rol64n(x, bits)	 __builtin_stdc_rotate_left(x, bits)
#define shim_rol128n(x, bits)	 __builtin_stdc_rotate_left(x, bits)
#else

#if defined(HAVE_BUILTIN_ROTATELEFT8)
#define shim_rol8n(x, bits)	 __builtin_rotateleft8(x, bits)
#elif defined(HAVE_INTRINSIC_ROLB) &&	\
      defined(HAVE_X86INTRIN_H)  &&	\
      !defined(HAVE_COMPILER_ICC)
#define shim_rol8n(x, bits)	__rolb(x, bits)
#else
static inline uint8_t CONST ALWAYS_INLINE shim_rol8n(const uint8_t x, const unsigned int bits)
{
	return ((x << bits) | x >> (8 - bits));
}
#endif

#if defined(HAVE_BUILTIN_ROTATELEFT16)
#define shim_rol16n(x, bits)	__builtin_rotateleft16(x, bits)
#elif defined(HAVE_INTRINSIC_ROLW) &&	\
      defined(HAVE_X86INTRIN_H) && 	\
      !defined(HAVE_COMPILER_ICC)
#define shim_rol16n(x, bits)	__rolw(x, bits)
#else
static inline uint16_t CONST ALWAYS_INLINE shim_rol16n(const uint16_t x, const unsigned int bits)
{
	return ((x << bits) | x >> (16 - bits));
}
#endif

#if defined(HAVE_BUILTIN_ROTATELEFT32)
#define shim_rol32n(x, bits)	__builtin_rotateleft32(x, bits)
#elif defined(HAVE_INTRINSIC_ROLD) &&	\
      defined(HAVE_X86INTRIN_H) && 	\
      !defined(HAVE_COMPILER_ICC)
#define shim_rol32n(x, bits)	__rold(x, bits)
#else
static inline uint32_t CONST ALWAYS_INLINE shim_rol32n(const uint32_t x, const unsigned int bits)
{
	return ((x << bits) | x >> (32 - bits));
}
#endif

#if defined(HAVE_BUILTIN_ROTATELEFT64)
#define shim_rol64n(x, bits)	__builtin_rotateleft64(x, bits)
#elif defined(HAVE_INTRINSIC_ROLQ) &&	\
      defined(HAVE_X86INTRIN_H) && 	\
      !defined(HAVE_COMPILER_ICC)
#define shim_rol64n(x, bits)	__rolq(x, bits)
#else
static inline uint64_t CONST ALWAYS_INLINE shim_rol64n(const uint64_t x, const unsigned int bits)
{
	return ((x << bits) | x >> (64 - bits));
}
#endif

#if defined(HAVE_INT128_T)
static inline __uint128_t CONST ALWAYS_INLINE shim_rol128n(const __uint128_t x, const unsigned int bits)
{
	return ((x << bits) | x >> (128 - bits));
}
#endif
#endif

#if defined(HAVE_BUILTIN_STDC_ROTATE_RIGHT)
#define shim_ror8n(x, bits)	__builtin_stdc_rotate_right(x, bits)
#define shim_ror16n(x, bits)	__builtin_stdc_rotate_right(x, bits)
#define shim_ror32n(x, bits)	__builtin_stdc_rotate_right(x, bits)
#define shim_ror64n(x, bits)	__builtin_stdc_rotate_right(x, bits)
#define shim_ror128n(x, bits)	__builtin_stdc_rotate_right(x, bits)
#else

#if defined(HAVE_BUILTIN_ROTATERIGHT8)
#define shim_ror8n(x, bits)	__builtin_rotateright8(x, bits)
#elif defined(HAVE_INTRINSIC_RORB) &&	\
      defined(HAVE_X86INTRIN_H) &&	\
      !defined(HAVE_COMPILER_ICC)
#define shim_ror8n(x, bits)	__rorb(x, bits)
#else
static inline uint8_t CONST ALWAYS_INLINE shim_ror8n(const uint8_t x, const unsigned int bits)
{
	return ((x >> bits) | x << (8 - bits));
}
#endif

#if defined(HAVE_BUILTIN_ROTATERIGHT16)
#define shim_ror16n(x, bits)	__builtin_rotateright16(x, bits)
#elif defined(HAVE_INTRINSIC_RORW) &&	\
      defined(HAVE_X86INTRIN_H) &&	\
      !defined(HAVE_COMPILER_ICC)
#define shim_ror16n(x, bits)	__rorw(x, bits)
#else
static inline uint16_t CONST ALWAYS_INLINE shim_ror16n(const uint16_t x, const unsigned int bits)
{
	return ((x >> bits) | x << (16 - bits));
}
#endif

#if defined(HAVE_BUILTIN_ROTATERIGHT32)
#define shim_ror32n(x, bits)	__builtin_rotateright32(x, bits)
#elif defined(HAVE_INTRINSIC_RORD) &&	\
      defined(HAVE_X86INTRIN_H) &&	\
      !defined(HAVE_COMPILER_ICC)
#define shim_ror32n(x, bits)	__rord(x, bits)
#else
static inline uint32_t CONST ALWAYS_INLINE shim_ror32n(const uint32_t x, const unsigned int bits)
{
	return ((x >> bits) | x << (32 - bits));
}
#endif

#if defined(HAVE_BUILTIN_ROTATERIGHT64)
#define shim_ror64n(x, bits)	__builtin_rotateright64(x, bits)
#elif defined(HAVE_INTRINSIC_RORQ) &&	\
      defined(HAVE_X86INTRIN_H) &&	\
      !defined(HAVE_COMPILER_ICC)
#define shim_ror64n(x, bits)	__rorq(x, bits)
#else
static inline uint64_t CONST ALWAYS_INLINE shim_ror64n(const uint64_t x, const unsigned int bits)
{
	return ((x >> bits) | x << (64 - bits));
}
#endif

#if defined(HAVE_INT128_T)
static inline __uint128_t CONST ALWAYS_INLINE shim_ror128n(const __uint128_t x, const unsigned int bits)
{
	return ((x >> bits) | x << (128 - bits));
}
#endif
#endif

#define shim_rol8(x)	shim_rol8n((x), 1)
#define shim_rol16(x)	shim_rol16n((x), 1)
#define shim_rol32(x)	shim_rol32n((x), 1)
#define shim_rol64(x)	shim_rol64n((x), 1)
#define shim_rol128(x)	shim_rol128n((x), 1)

#define shim_ror8(x)	shim_ror8n((x), 1)
#define shim_ror16(x)	shim_ror16n((x), 1)
#define shim_ror32(x)	shim_ror32n((x), 1)
#define shim_ror64(x)	shim_ror64n((x), 1)
#define shim_ror128(x)	shim_ror128n((x), 1)

#endif
