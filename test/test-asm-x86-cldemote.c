// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */

static inline void cldemote(void *p)
{
	__asm__ __volatile__("cldemote (%0)\n" : : "r"(p) : "memory");
}

int main(int argc, char **argv)
{
	char buf[64];

	cldemote(buf);

	return 0;
}
