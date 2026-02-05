/*
 * Copyright (C) 2022-2026 Colin Ian King
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
#ifndef CORE_PUT_H
#define CORE_PUT_H

extern stress_put_val_t g_put_val;	/* sync data to somewhere */

#define stress_put_bool(a)	stress_put_uint8((uint8_t)(a))

/*
 *  put_uint8()
 *	stash a uint8_t value
 */
static inline void ALWAYS_INLINE stress_put_uint8(const uint8_t a)
{
	g_put_val.uint8_val = a;
}

/*
 *  put_uint16()
 *	stash a uint16_t value
 */
static inline void ALWAYS_INLINE stress_put_uint16(const uint16_t a)
{
	g_put_val.uint16_val = a;
}

/*
 *  stress_put_uint32()
 *	stash a uint32_t value
 */
static inline void ALWAYS_INLINE stress_put_uint32(const uint32_t a)
{
	g_put_val.uint32_val = a;
}

/*
 *  stress_put_uint64()
 *	stash a uint64_t value
 */
static inline void ALWAYS_INLINE stress_put_uint64(const uint64_t a)
{
	g_put_val.uint64_val = a;
}

#if defined(HAVE_INT128_T)
/*
 *  stress_put_uint128()
 *	stash a uint128_t value
 */
static inline void ALWAYS_INLINE stress_put_uint128(const __uint128_t a)
{
	g_put_val.uint128_val = a;
}
#endif

/*
 *  stress_put_float()
 *	stash a float value
 */
static inline void ALWAYS_INLINE stress_put_float(const float a)
{
	g_put_val.float_val = a;
}

/*
 *  stress_put_double()
 *	stash a double value
 */
static inline void ALWAYS_INLINE stress_put_double(const double a)
{
	g_put_val.double_val = a;
}

/*
 *  stress_put_long_double()
 *	stash a double value
 */
static inline void ALWAYS_INLINE stress_put_long_double(const long double a)
{
	g_put_val.long_double_val = a;
}

/*
 *  stress_put_void_ptr()
 *	stash a void * pointer value
 */
static inline void ALWAYS_INLINE stress_put_void_ptr(volatile void * const a)
{
	g_put_val.void_ptr_val = a;
}

#endif
