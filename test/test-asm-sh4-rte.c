// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#if defined(__SH4__)
int main(void)
{
	__asm__ __volatile__("rte");

	return 0;
}
#else
#error not SH4 so no rte instruction
#endif
