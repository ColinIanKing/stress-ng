/*
 * Copyright (C) 2026 Colin Ian King.
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

/*
 *  Probe to see if the aarch64 ARM ACLE CRC32 intrinsics
 *  (e.g. __crc32cd) can be used with a per-function
 *  __attribute__((target("+crc"))) so that the CRC32 hardware
 *  instructions can be used without having to build the whole
 *  binary with a +crc -march option.
 *
 *  Running the produced binary requires the CPU to have the
 *  crc32 feature; the stressor does a run-time HWCAP check
 *  before using the hardware path, so the probe binary is
 *  only compiled, not run, by the configure step.
 */
#if defined(__aarch64__)

#include <arm_acle.h>

__attribute__((target("+crc")))
static unsigned int crc32_chain(unsigned int crc, const unsigned long *data, int n)
{
	int i;

	for (i = 0; i < n; i++)
		crc = __crc32cd(crc, data[i]);
	return crc;
}

int main(void)
{
	static const unsigned long data[4] = { 1, 2, 3, 4 };
	unsigned int crc = crc32_chain(0, data, 4);

	return (crc == 0) ? 1 : 0;
}
#else
#error aarch64 ARM ACLE CRC32 intrinsics not available
#endif
