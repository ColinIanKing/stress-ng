// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023      Colin Ian King
 *
 */

static inline void prefetchnta(void *p)
{
	__asm__ __volatile__("prefetchnta (%0)\n" : : "r"(p) : "memory");
}

int main(int argc, char **argv)
{
	char buf[64];

	prefetchnta(buf);

	return 0;
}
