/*
 * Copyright (C) 2022-2023 Colin Ian King
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

#if defined(HAVE_X86INTRIN_H)
#include <x86intrin.h>
#endif

#if defined(HAVE_BUILTIN_MEMSET)
#define shim_memset(s, c, n)		__builtin_memset(s, c, n)
#else
#define shim_memset(s, c, n)		memset(s, c, n)
#endif

#if defined(HAVE_BUILTIN_MEMCPY)
#define	shim_memcpy(dst, src, n)	__builtin_memcpy(dst, src, n)
#else
#define	shim_memcpy(dst, src, n)	memcpy(dst, src, n)
#endif

#if defined(HAVE_BUILTIN_MEMMOVE)
#define	shim_memmove(dst, src, n)	__builtin_memmove(dst, src, n)
#else
#define	shim_memmove(dst, src, n)	memmove(dst, src, n)
#endif

#if defined(HAVE_BUILTIN_MEMCMP)
#define	shim_memcmp(dst, src, n)	__builtin_memcmp(dst, src, n)
#else
#define	shim_memcmp(dst, src, n)	memcmp(dst, src, n)
#endif

#if defined(HAVE_BUILTIN_CABSL)
#define shim_cabsl(x)	__builtin_cabsl(x)
#else
#if defined(HAVE_CABSL)
#define shim_cabsl(x)	cabsl(x)
#else
#define shim_cabsl(x)	cabs(x)
#endif
#endif

#if defined(HAVE_BUILTIN_LGAMMAL)
#define shim_lgammal(x)	__builtin_lgammal(x)
#else
#if defined(HAVE_LGAMMAL)
#define shim_lgammal(x)	lgammal(x)
#else
#define shim_lgammal(x)	lgamma(x)
#endif
#endif

#if defined(HAVE_BUILTIN_CPOW)
#define shim_cpow(x, z)	__builtin_cpow(x, z)
#else
#if defined(HAVE_CPOW)
#define shim_cpow(x, z)	cpow(x, z)
#else
#define shim_cpow(x, z)	pow(x, z)
#endif
#endif

#if defined(HAVE_BUILTIN_POWL)
#define shim_powl(x, y)	__builtin_powl(x, y)
#else
#if defined(HAVE_POWL)
#define shim_powl(x, y)	powl(x, y)
#else
#define shim_powl(x, y)	pow(x, y)
#endif
#endif

#if defined(HAVE_BUILTIN_RINTL)
#define shim_rintl(x)	__builtin_rintl(x)
#else
#if defined(HAVE_RINTL)
#define shim_rintl(x)	rintl(x)
#else
#define shim_rintl(x)	shim_rint(x)
#endif
#endif

#if defined(HAVE_BUILTIN_LOG)
#define shim_log(x)	__builtin_log(x)
#else
#define shim_log(x)	log(x)
#endif

#if defined(HAVE_BUILTIN_LOGL)
#define shim_logl(x)	__builtin_logl(x)
#else
#if defined(HAVE_LOGL)
#define shim_logl(x)	logl(x)
#else
#define shim_logl(x)	shim_log(x)
#endif
#endif

#if defined(HAVE_BUILTIN_EXP)
#define shim_exp(x)	__builtin_exp(x)
#else
#define shim_exp(x)	exp(x)
#endif

#if defined(HAVE_BUILTIN_EXPL)
#define shim_expl(x)	__builtin_expl(x)
#else
#if defined(HAVE_EXPL) && !defined(__HAIKU__)
#define shim_expl(x)	expl(x)
#else
#define shim_expl(x)	shim_exp(x)
#endif
#endif

#if defined(HAVE_BUILTIN_CEXP)
#define shim_cexp(x)	__builtin_cexp(x)
#else
#define shim_cexp(x)	cexp(x)
#endif

#if defined(HAVE_BUILTIN_COSF)
#define shim_cosf(x)	__builtin_cosf(x)
#else
#define shim_cosf(x)	cosf(x)
#endif

#if defined(HAVE_BUILTIN_COS)
#define shim_cos(x)	__builtin_cos(x)
#else
#define shim_cos(x)	cos(x)
#endif

#if defined(HAVE_BUILTIN_COSL)
#define shim_cosl(x)	__builtin_cosl(x)
#else
#if defined(HAVE_COSL)
#define shim_cosl(x)	cosl(x)
#else
#define shim_cosl(x)	((long double)shim_cos((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_COSHL)
#define shim_coshl(x)	__builtin_coshl(x)
#else
#if defined(HAVE_COSHL)
#define shim_coshl(x)	coshl(x)
#else
#define shim_coshl(x)	((long double)cosh((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_CCOS)
#define shim_ccos(x)	__builtin_ccos(x)
#else
#if defined(HAVE_CCOS)
#define	shim_ccos(x)	ccos(x)
#else
#define	shim_ccos(x)	shim_cos((double)x)
#endif
#endif

#if defined(HAVE_BUILTIN_CCOSF)
#define shim_ccosf(x)	__builtin_ccosf(x)
#else
#if defined(HAVE_CCOSF)
#define	shim_ccosf(x)	ccosf(x)
#else
#define	shim_ccosf(x)	shim_ccos(x)
#endif
#endif

#if defined(HAVE_BUILTIN_CCOSL)
#define shim_ccosl(x)	__builtin_ccosl(x)
#else
#if defined(HAVE_CCOSL)
#define	shim_ccosl(x)	ccosl(x)
#else
#define	shim_ccosl(x)	((long double complex)shim_ccos((double complex)(x))
#endif
#endif

#if defined(HAVE_BUILTIN_SINF)
#define shim_sinf(x)	__builtin_sinf(x)
#else
#define shim_sinf(x)	sinf(x)
#endif

#if defined(HAVE_BUILTIN_SIN)
#define shim_sin(x)	__builtin_sin(x)
#else
#define shim_sin(x)	sin(x)
#endif

#if defined(HAVE_BUILTIN_SINL)
#define shim_sinl(x)	__builtin_sinl(x)
#else
#if defined(HAVE_SINL)
#define shim_sinl(x)	sinl(x)
#else
#define shim_sinl(x)	((long double)shim_sin((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_SINCOSF)
#define shim_sincosf(x, s, c)	__builtin_sincosf(x, s, c)
#else
#define shim_sincosf(x, s, c)	sincosf(x, s, c)
#endif

#if defined(HAVE_BUILTIN_SINCOS)
#define shim_sincos(x, s, c)	__builtin_sincos(x, s, c)
#else
#define shim_sincos(x, s, c)	sincos(x, s, c)
#endif

#if defined(HAVE_BUILTIN_SINCOSL)
#define shim_sincosl(x, s, c)	__builtin_sincosl(x, s, c)
#else
#define shim_sincosl(x, s, c)	sincosl(x, s, c)
#endif

#if defined(HAVE_BUILTIN_SINHL)
#define shim_sinhl(x)	__builtin_sinhl(x)
#else
#if defined(HAVE_SINHL)
#define shim_sinhl(x)	sinhl(x)
#else
#define shim_sinhl(x)	((long double)sinh((double)(x)))
#endif
#endif

#if defined(HAVE_BUILTIN_CSIN)
#define shim_csin(x)	__builtin_csin(x)
#else
#if defined(HAVE_CSIN)
#define	shim_csin(x)	csin(x)
#else
#define	shim_csin(x)	shim_sin((double)x)
#endif
#endif

#if defined(HAVE_BUILTIN_CSINF)
#define shim_csinf(x)	__builtin_csinf(x)
#else
#if defined(HAVE_CSINF)
#define	shim_csinf(x)	csinf(x)
#else
#define	shim_csinf(x)	shim_csin(x)
#endif
#endif

#if defined(HAVE_BUILTIN_CSINL)
#define shim_csinl(x)	__builtin_csinl(x)
#else
#if defined(HAVE_CSINL)
#define	shim_csinl(x)	csinl(x)
#else
#define	shim_csinl(x)	(long double complex)shim_csin((double complex)(x))
#endif
#endif

#if defined(HAVE_BUILTIN_TANF)
#define shim_tanf(x)	__builtin_tanf(x)
#else
#define shim_tanf(x)	tanf(x)
#endif

#if defined(HAVE_BUILTIN_TAN)
#define shim_tan(x)	__builtin_tan(x)
#else
#define shim_tan(x)	tan(x)
#endif

#if defined(HAVE_BUILTIN_TANL)
#define shim_tanl(x)	__builtin_tanl(x)
#else
#define shim_tanl(x)	tanl(x)
#endif

#if defined(HAVE_BUILTIN_SQRT)
#define shim_sqrt(x)	__builtin_sqrt(x)
#else
#define shim_sqrt(x)	sqrt(x)
#endif

#if defined(HAVE_BUILTIN_SQRTL)
#define shim_sqrtl(x)	__builtin_sqrtl(x)
#else
#if defined(HAVE_SQRTL)
#define shim_sqrtl(x)	sqrtl(x)
#else
#define shim_sqrtl(x)	((long double)shim_sqrt(x))
#endif
#endif

#if defined(HAVE_BUILTIN_FABS)
#define shim_fabs(x)	__builtin_fabs(x)
#else
#define shim_fabs(x)	fabs(x)
#endif

#if defined(HAVE_BUILTIN_FABSF)
#define shim_fabsf(x)	__builtin_fabsf(x)
#else
#define shim_fabsf(x)	fabsf(x)
#endif

#if defined(HAVE_BUILTIN_FABSL)
#define shim_fabsl(x)	__builtin_fabsl(x)
#else
#define shim_fabsl(x)	fabsl(x)
#endif

#if defined(HAVE_BUILTIN_RINT)
#define shim_rint(x)	__builtin_rint(x)
#else
#define shim_rint(x)	rint(x)
#endif

#if defined(HAVE_BUILTIN_ROUNDL)
#define shim_roundl(x)	__builtin_roundl(x)
#else
#define shim_roundl(x)	roundl(x)
#endif

#if defined(HAVE_BUILTIN_ROTATELEFT8)
#define shim_rol8n(x, bits)	 __builtin_rotateleft8(x, bits)
#elif defined(HAVE_INTRINSIC_ROLB) &&	\
      defined(HAVE_X86INTRIN_H)  &&	\
      !defined(HAVE_COMPILER_ICC)
#define shim_rol8n(x, bits)	__rolb(x, bits)
#else
static inline uint8_t shim_rol8n(const uint8_t x, const unsigned int bits)
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
static inline uint16_t shim_rol16n(const uint16_t x, const unsigned int bits)
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
static inline uint32_t shim_rol32n(const uint32_t x, const unsigned int bits)
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
static inline uint64_t shim_rol64n(const uint64_t x, const unsigned int bits)
{
	return ((x << bits) | x >> (64 - bits));
}
#endif

#if defined(HAVE_INT128_T)
static inline __uint128_t shim_rol128n(const __uint128_t x, const unsigned int bits)
{
	return ((x << bits) | x >> (128 - bits));
}
#endif

#if defined(HAVE_BUILTIN_ROTATERIGHT8)
#define shim_ror8n(x, bits)	__builtin_rotateright8(x, bits)
#elif defined(HAVE_INTRINSIC_RORB) &&	\
      defined(HAVE_X86INTRIN_H) &&	\
      !defined(HAVE_COMPILER_ICC)
#define shim_ror8n(x, bits)	__rorb(x, bits)
#else
static inline uint8_t shim_ror8n(const uint8_t x, const unsigned int bits)
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
static inline uint16_t shim_ror16n(const uint16_t x, const unsigned int bits)
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
static inline uint32_t shim_ror32n(const uint32_t x, const unsigned int bits)
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
static inline uint64_t shim_ror64n(const uint64_t x, const unsigned int bits)
{
	return ((x >> bits) | x << (64 - bits));
}
#endif

#if defined(HAVE_INT128_T)
static inline __uint128_t shim_ror128n(const __uint128_t x, const unsigned int bits)
{
	return ((x >> bits) | x << (128 - bits));
}
#endif

#define shim_rol8(x)	shim_rol8n(x, 1)
#define shim_rol16(x)	shim_rol16n(x, 1)
#define shim_rol32(x)	shim_rol32n(x, 1)
#define shim_rol64(x)	shim_rol64n(x, 1)
#define shim_rol128(x)	shim_rol128n(x, 1)

#define shim_ror8(x)	shim_ror8n(x, 1)
#define shim_ror16(x)	shim_ror16n(x, 1)
#define shim_ror32(x)	shim_ror32n(x, 1)
#define shim_ror64(x)	shim_ror64n(x, 1)
#define shim_ror128(x)	shim_ror128n(x, 1)

#endif
