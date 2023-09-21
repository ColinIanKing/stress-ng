/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#ifndef CORE_PUT_H
#define CORE_PUT_H

extern stress_put_val_t g_put_val;	/* sync data to somewhere */

/*
 *  uint8_put()
 *	stash a uint8_t value
 */
static inline void ALWAYS_INLINE stress_uint8_put(const uint8_t a)
{
	g_put_val.uint8_val = a;
}

/*
 *  uint16_put()
 *	stash a uint16_t value
 */
static inline void ALWAYS_INLINE stress_uint16_put(const uint16_t a)
{
	g_put_val.uint16_val = a;
}

/*
 *  stress_uint32_put()
 *	stash a uint32_t value
 */
static inline void ALWAYS_INLINE stress_uint32_put(const uint32_t a)
{
	g_put_val.uint32_val = a;
}

/*
 *  stress_uint64_put()
 *	stash a uint64_t value
 */
static inline void ALWAYS_INLINE stress_uint64_put(const uint64_t a)
{
	g_put_val.uint64_val = a;
}

#if defined(HAVE_INT128_T)
/*
 *  stress_uint128_put()
 *	stash a uint128_t value
 */
static inline void ALWAYS_INLINE stress_uint128_put(const __uint128_t a)
{
	g_put_val.uint128_val = a;
}
#endif

/*
 *  stress_float_put()
 *	stash a float value
 */
static inline void ALWAYS_INLINE stress_float_put(const float a)
{
	g_put_val.float_val = a;
}

/*
 *  stress_double_put()
 *	stash a double value
 */
static inline void ALWAYS_INLINE stress_double_put(const double a)
{
	g_put_val.double_val = a;
}

/*
 *  stress_long_double_put()
 *	stash a double value
 */
static inline void ALWAYS_INLINE stress_long_double_put(const long double a)
{
	g_put_val.long_double_val = a;
}

/*
 *  stress_void_ptr_put()
 *	stash a void * pointer value
 */
static inline void ALWAYS_INLINE stress_void_ptr_put(volatile void * const a)
{
	g_put_val.void_ptr_val = a;
}

#endif
