// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King
 *
 */

#if defined(__x86_64__) || defined(__x86_64) || \
    defined(__amd64__)  || defined(__amd64)

static inline void repzero(void *ptr, const int n)
{
	__asm__ __volatile__(
		"mov $0xaaaaaaaa,%%eax\n;"
		"mov %0,%%rdi\n;"
		"mov %1,%%ecx\n;"
		"rep stosl %%eax,%%es:(%%rdi);\n"
		:
		: "m" (ptr),
		  "m" (n)
		: "ecx","rdi","eax");
}

int main(void)
{
	char buffer[1024];

	repzero(buffer, sizeof(buffer));
}
#else
#error not an x86 so no rep stosd instruction
#endif
