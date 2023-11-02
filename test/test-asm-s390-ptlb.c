// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#if defined(__s390__)
int main(void)
{
	__asm__ __volatile__("ptlb" : : : "memory");

	return 0;
}
#else
#error not S390 so no ptlb instruction
#endif
