/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static const help_t help[] = {
	{ NULL,	"branch N",	"start N workers that force branch misprediction" },
	{ NULL,	"branch-ops N",	"stop after N branch misprediction branches" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LABEL_AS_VALUE)
/*
 *  jmp_mwc8()
 *	special non-overly optimized mwc8 that gets inlined
 *	to remove a jmp and hence boost branch miss rates.
 *	Do not optimize this any further as this will lower
 *	the branch miss rate.
 */
static inline uint8_t jmp_mwc8(void)
{
	static uint32_t w = MWC_SEED_W;
	static uint32_t z = MWC_SEED_Z;

        z = 36969 * (z & 65535) + (z >> 16);
        w = 18000 * (w & 65535) + (w >> 16);
        return (w >> 3) & 0xff;
}

/*
 *  The following jumps to a random label. If do_more is false
 *  then we jump to label ret and abort. This has been carefully
 *  hand crafted to make each JMP() macro expand to code with
 *  just one jmp statement
 */
#define JMP(a)	label ## a: 				\
{							\
	register bool do_more;				\
	register uint16_t _index = jmp_mwc8();		\
							\
	inc_counter(args);				\
	do_more = LIKELY((int)g_keep_stressing_flag) &	\
		(((int)!args->max_ops) | 		\
		 (get_counter(args) < args->max_ops));	\
	_index |= (do_more << 8);			\
	goto *labels[_index];				\
}

/*
 *  stress_branch()
 *	stress instruction branch prediction
 */
static int stress_branch(const args_t *args)
{
	static const void ALIGN64 *labels[] = {
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,

		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,

		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,

		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret,

		&&label0x00, &&label0x01, &&label0x02, &&label0x03,
		&&label0x04, &&label0x05, &&label0x06, &&label0x07,
		&&label0x08, &&label0x09, &&label0x0a, &&label0x0b,
		&&label0x0c, &&label0x0d, &&label0x0e, &&label0x0f,
		&&label0x10, &&label0x11, &&label0x12, &&label0x13,
		&&label0x14, &&label0x15, &&label0x16, &&label0x17,
		&&label0x18, &&label0x19, &&label0x1a, &&label0x1b,
		&&label0x1c, &&label0x1d, &&label0x1e, &&label0x1f,
		&&label0x20, &&label0x21, &&label0x22, &&label0x23,
		&&label0x24, &&label0x25, &&label0x26, &&label0x27,
		&&label0x28, &&label0x29, &&label0x2a, &&label0x2b,
		&&label0x2c, &&label0x2d, &&label0x2e, &&label0x2f,
		&&label0x30, &&label0x31, &&label0x32, &&label0x33,
		&&label0x34, &&label0x35, &&label0x36, &&label0x37,
		&&label0x38, &&label0x39, &&label0x3a, &&label0x3b,
		&&label0x3c, &&label0x3d, &&label0x3e, &&label0x3f,

		&&label0x40, &&label0x41, &&label0x42, &&label0x43,
		&&label0x44, &&label0x45, &&label0x46, &&label0x47,
		&&label0x48, &&label0x49, &&label0x4a, &&label0x4b,
		&&label0x4c, &&label0x4d, &&label0x4e, &&label0x4f,
		&&label0x50, &&label0x51, &&label0x52, &&label0x53,
		&&label0x54, &&label0x55, &&label0x56, &&label0x57,
		&&label0x58, &&label0x59, &&label0x5a, &&label0x5b,
		&&label0x5c, &&label0x5d, &&label0x5e, &&label0x5f,
		&&label0x60, &&label0x61, &&label0x62, &&label0x63,
		&&label0x64, &&label0x65, &&label0x66, &&label0x67,
		&&label0x68, &&label0x69, &&label0x6a, &&label0x6b,
		&&label0x6c, &&label0x6d, &&label0x6e, &&label0x6f,
		&&label0x70, &&label0x71, &&label0x72, &&label0x73,
		&&label0x74, &&label0x75, &&label0x76, &&label0x77,
		&&label0x78, &&label0x79, &&label0x7a, &&label0x7b,
		&&label0x7c, &&label0x7d, &&label0x7e, &&label0x7f,

		&&label0x80, &&label0x81, &&label0x82, &&label0x83,
		&&label0x84, &&label0x85, &&label0x86, &&label0x87,
		&&label0x88, &&label0x89, &&label0x8a, &&label0x8b,
		&&label0x8c, &&label0x8d, &&label0x8e, &&label0x8f,
		&&label0x90, &&label0x91, &&label0x92, &&label0x93,
		&&label0x94, &&label0x95, &&label0x96, &&label0x97,
		&&label0x98, &&label0x99, &&label0x9a, &&label0x9b,
		&&label0x9c, &&label0x9d, &&label0x9e, &&label0x9f,
		&&label0xa0, &&label0xa1, &&label0xa2, &&label0xa3,
		&&label0xa4, &&label0xa5, &&label0xa6, &&label0xa7,
		&&label0xa8, &&label0xa9, &&label0xaa, &&label0xab,
		&&label0xac, &&label0xad, &&label0xae, &&label0xaf,
		&&label0xb0, &&label0xb1, &&label0xb2, &&label0xb3,
		&&label0xb4, &&label0xb5, &&label0xb6, &&label0xb7,
		&&label0xb8, &&label0xb9, &&label0xba, &&label0xbb,
		&&label0xbc, &&label0xbd, &&label0xbe, &&label0xbf,

		&&label0xc0, &&label0xc1, &&label0xc2, &&label0xc3,
		&&label0xc4, &&label0xc5, &&label0xc6, &&label0xc7,
		&&label0xc8, &&label0xc9, &&label0xca, &&label0xcb,
		&&label0xcc, &&label0xcd, &&label0xce, &&label0xcf,
		&&label0xd0, &&label0xd1, &&label0xd2, &&label0xd3,
		&&label0xd4, &&label0xd5, &&label0xd6, &&label0xd7,
		&&label0xd8, &&label0xd9, &&label0xda, &&label0xdb,
		&&label0xdc, &&label0xdd, &&label0xde, &&label0xdf,
		&&label0xe0, &&label0xe1, &&label0xe2, &&label0xe3,
		&&label0xe4, &&label0xe5, &&label0xe6, &&label0xe7,
		&&label0xe8, &&label0xe9, &&label0xea, &&label0xeb,
		&&label0xec, &&label0xed, &&label0xee, &&label0xef,
		&&label0xf0, &&label0xf1, &&label0xf2, &&label0xf3,
		&&label0xf4, &&label0xf5, &&label0xf6, &&label0xf7,
		&&label0xf8, &&label0xf9, &&label0xfa, &&label0xfb,
		&&label0xfc, &&label0xfd, &&label0xfe, &&label0xff,
	};

	for (;;) {
		JMP(0x00) JMP(0x01) JMP(0x02) JMP(0x03)
		JMP(0x04) JMP(0x05) JMP(0x06) JMP(0x07)
		JMP(0x08) JMP(0x09) JMP(0x0a) JMP(0x0b)
		JMP(0x0c) JMP(0x0d) JMP(0x0e) JMP(0x0f)
		JMP(0x10) JMP(0x11) JMP(0x12) JMP(0x13)
		JMP(0x14) JMP(0x15) JMP(0x16) JMP(0x17)
		JMP(0x18) JMP(0x19) JMP(0x1a) JMP(0x1b)
		JMP(0x1c) JMP(0x1d) JMP(0x1e) JMP(0x1f)
		JMP(0x20) JMP(0x21) JMP(0x22) JMP(0x23)
		JMP(0x24) JMP(0x25) JMP(0x26) JMP(0x27)
		JMP(0x28) JMP(0x29) JMP(0x2a) JMP(0x2b)
		JMP(0x2c) JMP(0x2d) JMP(0x2e) JMP(0x2f)
		JMP(0x30) JMP(0x31) JMP(0x32) JMP(0x33)
		JMP(0x34) JMP(0x35) JMP(0x36) JMP(0x37)
		JMP(0x38) JMP(0x39) JMP(0x3a) JMP(0x3b)
		JMP(0x3c) JMP(0x3d) JMP(0x3e) JMP(0x3f)

		JMP(0x40) JMP(0x41) JMP(0x42) JMP(0x43)
		JMP(0x44) JMP(0x45) JMP(0x46) JMP(0x47)
		JMP(0x48) JMP(0x49) JMP(0x4a) JMP(0x4b)
		JMP(0x4c) JMP(0x4d) JMP(0x4e) JMP(0x4f)
		JMP(0x50) JMP(0x51) JMP(0x52) JMP(0x53)
		JMP(0x54) JMP(0x55) JMP(0x56) JMP(0x57)
		JMP(0x58) JMP(0x59) JMP(0x5a) JMP(0x5b)
		JMP(0x5c) JMP(0x5d) JMP(0x5e) JMP(0x5f)
		JMP(0x60) JMP(0x61) JMP(0x62) JMP(0x63)
		JMP(0x64) JMP(0x65) JMP(0x66) JMP(0x67)
		JMP(0x68) JMP(0x69) JMP(0x6a) JMP(0x6b)
		JMP(0x6c) JMP(0x6d) JMP(0x6e) JMP(0x6f)
		JMP(0x70) JMP(0x71) JMP(0x72) JMP(0x73)
		JMP(0x74) JMP(0x75) JMP(0x76) JMP(0x77)
		JMP(0x78) JMP(0x79) JMP(0x7a) JMP(0x7b)
		JMP(0x7c) JMP(0x7d) JMP(0x7e) JMP(0x7f)

		JMP(0x80) JMP(0x81) JMP(0x82) JMP(0x83)
		JMP(0x84) JMP(0x85) JMP(0x86) JMP(0x87)
		JMP(0x88) JMP(0x89) JMP(0x8a) JMP(0x8b)
		JMP(0x8c) JMP(0x8d) JMP(0x8e) JMP(0x8f)
		JMP(0x90) JMP(0x91) JMP(0x92) JMP(0x93)
		JMP(0x94) JMP(0x95) JMP(0x96) JMP(0x97)
		JMP(0x98) JMP(0x99) JMP(0x9a) JMP(0x9b)
		JMP(0x9c) JMP(0x9d) JMP(0x9e) JMP(0x9f)
		JMP(0xa0) JMP(0xa1) JMP(0xa2) JMP(0xa3)
		JMP(0xa4) JMP(0xa5) JMP(0xa6) JMP(0xa7)
		JMP(0xa8) JMP(0xa9) JMP(0xaa) JMP(0xab)
		JMP(0xac) JMP(0xad) JMP(0xae) JMP(0xaf)
		JMP(0xb0) JMP(0xb1) JMP(0xb2) JMP(0xb3)
		JMP(0xb4) JMP(0xb5) JMP(0xb6) JMP(0xb7)
		JMP(0xb8) JMP(0xb9) JMP(0xba) JMP(0xbb)
		JMP(0xbc) JMP(0xbd) JMP(0xbe) JMP(0xbf)

		JMP(0xc0) JMP(0xc1) JMP(0xc2) JMP(0xc3)
		JMP(0xc4) JMP(0xc5) JMP(0xc6) JMP(0xc7)
		JMP(0xc8) JMP(0xc9) JMP(0xca) JMP(0xcb)
		JMP(0xcc) JMP(0xcd) JMP(0xce) JMP(0xcf)
		JMP(0xd0) JMP(0xd1) JMP(0xd2) JMP(0xd3)
		JMP(0xd4) JMP(0xd5) JMP(0xd6) JMP(0xd7)
		JMP(0xd8) JMP(0xd9) JMP(0xda) JMP(0xdb)
		JMP(0xdc) JMP(0xdd) JMP(0xde) JMP(0xdf)
		JMP(0xe0) JMP(0xe1) JMP(0xe2) JMP(0xe3)
		JMP(0xe4) JMP(0xe5) JMP(0xe6) JMP(0xe7)
		JMP(0xe8) JMP(0xe9) JMP(0xea) JMP(0xeb)
		JMP(0xec) JMP(0xed) JMP(0xee) JMP(0xef)
		JMP(0xf0) JMP(0xf1) JMP(0xf2) JMP(0xf3)
		JMP(0xf4) JMP(0xf5) JMP(0xf6) JMP(0xf7)
		JMP(0xf8) JMP(0xf9) JMP(0xfa) JMP(0xfb)
		JMP(0xfc) JMP(0xfd) JMP(0xfe) JMP(0xff)
	}
ret:
	return EXIT_SUCCESS;
}

stressor_info_t stress_branch_info = {
	.stressor = stress_branch,
	.class = CLASS_CPU,
	.help = help
};
#else
stressor_info_t stress_branch_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU,
	.help = help
};
#endif
