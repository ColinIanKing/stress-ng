// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

static inline void clwb(void *p)
{
	__asm__ __volatile__("clwb (%0)\n" : : "r"(p) : "memory");
}

int main(int argc, char **argv)
{
	char buf[64];

	clwb(buf);

	return 0;
}
