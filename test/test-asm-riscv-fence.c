// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */

#if defined(__riscv) || \
    defined(__riscv__)
int main(void)
{
	__asm__ __volatile__("fence" ::: "memory");

	return 0;
}
#else
#error not RISC-V so no fence instruction
#endif
