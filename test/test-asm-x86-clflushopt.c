// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */

static inline void clflushopt(void *p)
{
	__asm__ __volatile__("clflushopt (%0)\n" : : "r"(p) : "memory");
}

int main(int argc, char **argv)
{
	char buf[64];

	clflushopt(buf);

	return 0;
}
