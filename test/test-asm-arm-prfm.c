/*
 * Copyright (C) 2025 SiPearl
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

#if defined(__ARM_ARCH_8A__) || defined(__aarch64__)
int main(void)
{
    int a = 0;
    __asm__ __volatile__("prfm PLDL1KEEP, [%0]\n" : : "r" (&a) : "memory", "cc");

    return 0;
}
#else
#error not an ARMv8 so no prfm instruction
#endif
