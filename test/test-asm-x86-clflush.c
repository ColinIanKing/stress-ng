// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */

static inline void clflush(void *ptr)
{
	__asm__ __volatile__("clflush (%0)\n" : : "r"(ptr) : "memory");
}

int main(int argc, char **argv)
{
	char buf[64];

	clflush(buf);

	return 0;
}
