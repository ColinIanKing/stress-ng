/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C)      2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

#ifndef CORE_ASM_RET_H
#define CORE_ASM_RET_H

typedef struct {
	const size_t stride;		/* Bytes between each function */
	const size_t len;		/* Length of return function */
	const char *assembler;		/* Assembler */
	const uint8_t opcodes[];	/* Opcodes of return function */
} stress_ret_opcode_t;

typedef void (*stress_ret_func_t)(void);

extern stress_ret_opcode_t stress_ret_opcode;
extern int stress_asm_ret_supported(const char *name);

#endif
