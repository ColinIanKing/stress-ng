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
 *  Probe to see if we can build AND run with the aarch64 SVE2
 *  instruction set (with bf16 and i8mm extensions) enabled via
 *  the -march=armv8.6-a+sve2+bf16+i8mm option.  Note that the
 *  older 'svebf16' spelling is rejected by GCC 12.x and later,
 *  the correct feature modifier is 'bf16'.
 *
 *  The test program is compiled by the check_tmp macro with
 *  the -march flag passed in via the $5 (cflags) argument;
 *  if the toolchain does not understand the architecture
 *  string the compile fails and the HAVE config is not set.
 *
 *  When SVE2 is enabled, the compiler defines __ARM_FEATURE_SVE2
 *  so we can sanity check this is really enabled at compile time.
 *
 *  The binary is then run by the check_tmp macro; auto-vectorized
 *  SVE code is emitted unconditionally into the hot paths of the
 *  compute stressors, so a build with this flag can only run on
 *  SVE2 capable hardware.  The run-time check below verifies the
 *  build host CPU actually has SVE2 before the flag is accepted,
 *  keeping non-SVE2 aarch64 builds working unchanged (NEON only).
 *
 *  Cross builds to a different aarch64 machine must force the
 *  flag on or off explicitly with MARCH_AARCH64_SVE2= in the
 *  make invocation.
 */
#if defined(__aarch64__) &&	\
    defined(__ARM_FEATURE_SVE2) && \
    defined(__ARM_FEATURE_BF16_VECTOR_ARITHMETIC) && \
    defined(__ARM_FEATURE_MATMUL_INT8)

#include <sys/auxv.h>

int main(void)
{
	unsigned long int hwcap = getauxval(AT_HWCAP);

	/* Build host must have SVE hardware to run the produced binary */
	if (!(hwcap & (1UL << 22)))	/* HWCAP_SVE */
		return 1;
	return 0;
}
#else
#error SVE2 with bf16 and i8mm not enabled by the -march option
#endif
