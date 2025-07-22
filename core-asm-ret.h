/*
 * Copyright (C) 2023-2025 Colin Ian King.
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
#ifndef CORE_ASM_RET_H
#define CORE_ASM_RET_H

typedef struct {
	const size_t stride;		/* Bytes between each function */
	const size_t len;		/* Length of return function */
	const char *assembler;		/* Assembler */
	const uint8_t opcodes[];	/* Opcodes of return function */
} stress_ret_opcode_t;

typedef void (*stress_ret_func_t)(void);

extern const stress_ret_opcode_t stress_ret_opcode;
extern WARN_UNUSED int stress_asm_ret_supported(const char *name);

#endif
