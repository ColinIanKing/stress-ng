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
		"mov $0x00,%%al\n;"
		"mov %0,%%rdi\n;"
		"mov %1,%%ecx\n;"
		"rep stosb %%al,%%es:(%%rdi);\n"
		:
		: "m" (ptr),
		  "m" (n)
		: "ecx","rdi","al");
}

int main(void)
{
	char buffer[1024];

	repzero(buffer, sizeof(buffer));
}
#else
#error not an x86 so no rep stosb instruction
#endif
