/*
 * Copyright (C) 2024      Colin Ian King.
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
#ifndef CORE_MWC_H
#define CORE_MWC_H

#include <stdint.h>
#include "core-attribute.h"

extern void stress_mwc_reseed(void);
extern void stress_mwc_set_seed(const uint32_t w, const uint32_t z);
extern void stress_mwc_get_seed(uint32_t *w, uint32_t *z);
extern void stress_mwc_seed(void);

extern uint8_t stress_mwc1(void);
extern uint8_t stress_mwc8(void);
extern uint16_t stress_mwc16(void);
extern uint32_t stress_mwc32(void);
extern uint64_t stress_mwc64(void);

extern uint8_t stress_mwc8modn(const uint8_t max);
extern uint16_t stress_mwc16modn(const uint16_t max);
extern uint32_t stress_mwc32modn(const uint32_t max);
extern uint64_t stress_mwc64modn(const uint64_t max);

extern void stress_rndbuf(void *buf, const size_t len);
extern void stress_rndstr(char *str, const size_t len);

#endif
