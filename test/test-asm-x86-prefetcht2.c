// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023      Colin Ian King
 *
 */

static inline void prefetcht2(void *p)
{
	__asm__ __volatile__("prefetcht2 (%0)\n" : : "r"(p) : "memory");
}

int main(int argc, char **argv)
{
	char buf[64];

	prefetcht2(buf);

	return 0;
}
