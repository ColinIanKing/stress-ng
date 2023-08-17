// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023      Colin Ian King
 *
 */

static inline void prefetcht1(void *p)
{
	__asm__ __volatile__("prefetcht1 (%0)\n" : : "r"(p) : "memory");
}

int main(int argc, char **argv)
{
	char buf[64];

	prefetcht1(buf);

	return 0;
}
