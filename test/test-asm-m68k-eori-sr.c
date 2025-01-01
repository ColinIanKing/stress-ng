/*
 * Copyright (C) 2022-2025 Colin Ian King
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

/* Arch specific M68K */
#if defined(__m68k__) ||	\
    defined(__mc68000__) ||	\
    defined(__mc68010__) ||	\
    defined(__mc68020__)
int main(void)
{
	__asm__ __volatile__("eori.w #0001,%sr");

	return 0;
}
#else
#error not m68k so no eori.w #0001,sr instruction
#endif
