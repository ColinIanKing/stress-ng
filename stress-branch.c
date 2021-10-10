/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"branch N",	"start N workers that force branch misprediction" },
	{ NULL,	"branch-ops N",	"stop after N branch misprediction branches" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LABEL_AS_VALUE)
/*
 *  jmp_stress_mwc8()
 *	special non-overly optimized stress_mwc8 that gets inlined
 *	to remove a jmp and hence boost branch miss rates.
 *	Do not optimize this any further as this will lower
 *	the branch miss rate.
 */
static inline uint16_t OPTIMIZE3 jmp_stress_mwc8(void)
{
	static uint32_t w = STRESS_MWC_SEED_W;
	static uint32_t z = STRESS_MWC_SEED_Z;

	z = 36969 * (z & 65535) + (z >> 16);
	w = 18000 * (w & 65535) + (w >> 16);
	return (w >> 3) & 0x1ff;
}

/*
 *  The following jumps to a random label. If do_more is false
 *  then we jump to label ret and abort. This has been carefully
 *  hand crafted to make each JMP() macro expand to code with
 *  just one jmp statement
 */
#undef J
#define J(a)   						\
a: 	idx = jmp_stress_mwc8();			\
	flag = keep_stressing_flag();			\
							\
	inc_counter(args);				\
	do_more = LIKELY(flag) &			\
		(((int)!args->max_ops) | 		\
		 (get_counter(args) < args->max_ops));	\
	idx |= (do_more << 9);				\
	goto *labels[idx];				\

/*
 *  stress_branch()
 *	stress instruction branch prediction
 */
static int OPTIMIZE3 stress_branch(const stress_args_t *args)
{
	static const void ALIGN64 *labels[] = {
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,

		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,

		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,

		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,

		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,

		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,

		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,

		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,
		&&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret, &&ret,

		&&L000, &&L001, &&L002, &&L003, &&L004, &&L005, &&L006, &&L007,
		&&L008, &&L009, &&L00a, &&L00b, &&L00c, &&L00d, &&L00e, &&L00f,
		&&L010, &&L011, &&L012, &&L013, &&L014, &&L015, &&L016, &&L017,
		&&L018, &&L019, &&L01a, &&L01b, &&L01c, &&L01d, &&L01e, &&L01f,
		&&L020, &&L021, &&L022, &&L023, &&L024, &&L025, &&L026, &&L027,
		&&L028, &&L029, &&L02a, &&L02b, &&L02c, &&L02d, &&L02e, &&L02f,
		&&L030, &&L031, &&L032, &&L033, &&L034, &&L035, &&L036, &&L037,
		&&L038, &&L039, &&L03a, &&L03b, &&L03c, &&L03d, &&L03e, &&L03f,

		&&L040, &&L041, &&L042, &&L043, &&L044, &&L045, &&L046, &&L047,
		&&L048, &&L049, &&L04a, &&L04b, &&L04c, &&L04d, &&L04e, &&L04f,
		&&L050, &&L051, &&L052, &&L053, &&L054, &&L055, &&L056, &&L057,
		&&L058, &&L059, &&L05a, &&L05b, &&L05c, &&L05d, &&L05e, &&L05f,
		&&L060, &&L061, &&L062, &&L063, &&L064, &&L065, &&L066, &&L067,
		&&L068, &&L069, &&L06a, &&L06b, &&L06c, &&L06d, &&L06e, &&L06f,
		&&L070, &&L071, &&L072, &&L073, &&L074, &&L075, &&L076, &&L077,
		&&L078, &&L079, &&L07a, &&L07b, &&L07c, &&L07d, &&L07e, &&L07f,

		&&L080, &&L081, &&L082, &&L083, &&L084, &&L085, &&L086, &&L087,
		&&L088, &&L089, &&L08a, &&L08b, &&L08c, &&L08d, &&L08e, &&L08f,
		&&L090, &&L091, &&L092, &&L093, &&L094, &&L095, &&L096, &&L097,
		&&L098, &&L099, &&L09a, &&L09b, &&L09c, &&L09d, &&L09e, &&L09f,
		&&L0a0, &&L0a1, &&L0a2, &&L0a3, &&L0a4, &&L0a5, &&L0a6, &&L0a7,
		&&L0a8, &&L0a9, &&L0aa, &&L0ab, &&L0ac, &&L0ad, &&L0ae, &&L0af,
		&&L0b0, &&L0b1, &&L0b2, &&L0b3, &&L0b4, &&L0b5, &&L0b6, &&L0b7,
		&&L0b8, &&L0b9, &&L0ba, &&L0bb, &&L0bc, &&L0bd, &&L0be, &&L0bf,

		&&L0c0, &&L0c1, &&L0c2, &&L0c3, &&L0c4, &&L0c5, &&L0c6, &&L0c7,
		&&L0c8, &&L0c9, &&L0ca, &&L0cb, &&L0cc, &&L0cd, &&L0ce, &&L0cf,
		&&L0d0, &&L0d1, &&L0d2, &&L0d3, &&L0d4, &&L0d5, &&L0d6, &&L0d7,
		&&L0d8, &&L0d9, &&L0da, &&L0db, &&L0dc, &&L0dd, &&L0de, &&L0df,
		&&L0e0, &&L0e1, &&L0e2, &&L0e3, &&L0e4, &&L0e5, &&L0e6, &&L0e7,
		&&L0e8, &&L0e9, &&L0ea, &&L0eb, &&L0ec, &&L0ed, &&L0ee, &&L0ef,
		&&L0f0, &&L0f1, &&L0f2, &&L0f3, &&L0f4, &&L0f5, &&L0f6, &&L0f7,
		&&L0f8, &&L0f9, &&L0fa, &&L0fb, &&L0fc, &&L0fd, &&L0fe, &&L0ff,

		&&L100, &&L101, &&L102, &&L103, &&L104, &&L105, &&L106, &&L107,
		&&L108, &&L109, &&L10a, &&L10b, &&L10c, &&L10d, &&L10e, &&L10f,
		&&L110, &&L111, &&L112, &&L113, &&L114, &&L115, &&L116, &&L117,
		&&L118, &&L119, &&L11a, &&L11b, &&L11c, &&L11d, &&L11e, &&L11f,
		&&L120, &&L121, &&L122, &&L123, &&L124, &&L125, &&L126, &&L127,
		&&L128, &&L129, &&L12a, &&L12b, &&L12c, &&L12d, &&L12e, &&L12f,
		&&L130, &&L131, &&L132, &&L133, &&L134, &&L135, &&L136, &&L137,
		&&L138, &&L139, &&L13a, &&L13b, &&L13c, &&L13d, &&L13e, &&L13f,

		&&L140, &&L141, &&L142, &&L143, &&L144, &&L145, &&L146, &&L147,
		&&L148, &&L149, &&L14a, &&L14b, &&L14c, &&L14d, &&L14e, &&L14f,
		&&L150, &&L151, &&L152, &&L153, &&L154, &&L155, &&L156, &&L157,
		&&L158, &&L159, &&L15a, &&L15b, &&L15c, &&L15d, &&L15e, &&L15f,
		&&L160, &&L161, &&L162, &&L163, &&L164, &&L165, &&L166, &&L167,
		&&L168, &&L169, &&L16a, &&L16b, &&L16c, &&L16d, &&L16e, &&L16f,
		&&L170, &&L171, &&L172, &&L173, &&L174, &&L175, &&L176, &&L177,
		&&L178, &&L179, &&L17a, &&L17b, &&L17c, &&L17d, &&L17e, &&L17f,

		&&L180, &&L181, &&L182, &&L183, &&L184, &&L185, &&L186, &&L187,
		&&L188, &&L189, &&L18a, &&L18b, &&L18c, &&L18d, &&L18e, &&L18f,
		&&L190, &&L191, &&L192, &&L193, &&L194, &&L195, &&L196, &&L197,
		&&L198, &&L199, &&L19a, &&L19b, &&L19c, &&L19d, &&L19e, &&L19f,
		&&L1a0, &&L1a1, &&L1a2, &&L1a3, &&L1a4, &&L1a5, &&L1a6, &&L1a7,
		&&L1a8, &&L1a9, &&L1aa, &&L1ab, &&L1ac, &&L1ad, &&L1ae, &&L1af,
		&&L1b0, &&L1b1, &&L1b2, &&L1b3, &&L1b4, &&L1b5, &&L1b6, &&L1b7,
		&&L1b8, &&L1b9, &&L1ba, &&L1bb, &&L1bc, &&L1bd, &&L1be, &&L1bf,

		&&L1c0, &&L1c1, &&L1c2, &&L1c3, &&L1c4, &&L1c5, &&L1c6, &&L1c7,
		&&L1c8, &&L1c9, &&L1ca, &&L1cb, &&L1cc, &&L1cd, &&L1ce, &&L1cf,
		&&L1d0, &&L1d1, &&L1d2, &&L1d3, &&L1d4, &&L1d5, &&L1d6, &&L1d7,
		&&L1d8, &&L1d9, &&L1da, &&L1db, &&L1dc, &&L1dd, &&L1de, &&L1df,
		&&L1e0, &&L1e1, &&L1e2, &&L1e3, &&L1e4, &&L1e5, &&L1e6, &&L1e7,
		&&L1e8, &&L1e9, &&L1ea, &&L1eb, &&L1ec, &&L1ed, &&L1ee, &&L1ef,
		&&L1f0, &&L1f1, &&L1f2, &&L1f3, &&L1f4, &&L1f5, &&L1f6, &&L1f7,
		&&L1f8, &&L1f9, &&L1fa, &&L1fb, &&L1fc, &&L1fd, &&L1fe, &&L1ff,
	};

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (;;) {
		register bool do_more;	
		register uint16_t idx;
		register bool flag;

		J(L000) J(L001) J(L002) J(L003) J(L004) J(L005) J(L006) J(L007)
		J(L008) J(L009) J(L00a) J(L00b) J(L00c) J(L00d) J(L00e) J(L00f)
		J(L010) J(L011) J(L012) J(L013) J(L014) J(L015) J(L016) J(L017)
		J(L018) J(L019) J(L01a) J(L01b) J(L01c) J(L01d) J(L01e) J(L01f)
		J(L020) J(L021) J(L022) J(L023) J(L024) J(L025) J(L026) J(L027)
		J(L028) J(L029) J(L02a) J(L02b) J(L02c) J(L02d) J(L02e) J(L02f)
		J(L030) J(L031) J(L032) J(L033) J(L034) J(L035) J(L036) J(L037)
		J(L038) J(L039) J(L03a) J(L03b) J(L03c) J(L03d) J(L03e) J(L03f)

		J(L040) J(L041) J(L042) J(L043) J(L044) J(L045) J(L046) J(L047)
		J(L048) J(L049) J(L04a) J(L04b) J(L04c) J(L04d) J(L04e) J(L04f)
		J(L050) J(L051) J(L052) J(L053) J(L054) J(L055) J(L056) J(L057)
		J(L058) J(L059) J(L05a) J(L05b) J(L05c) J(L05d) J(L05e) J(L05f)
		J(L060) J(L061) J(L062) J(L063) J(L064) J(L065) J(L066) J(L067)
		J(L068) J(L069) J(L06a) J(L06b) J(L06c) J(L06d) J(L06e) J(L06f)
		J(L070) J(L071) J(L072) J(L073) J(L074) J(L075) J(L076) J(L077)
		J(L078) J(L079) J(L07a) J(L07b) J(L07c) J(L07d) J(L07e) J(L07f)

		J(L080) J(L081) J(L082) J(L083) J(L084) J(L085) J(L086) J(L087)
		J(L088) J(L089) J(L08a) J(L08b) J(L08c) J(L08d) J(L08e) J(L08f)
		J(L090) J(L091) J(L092) J(L093) J(L094) J(L095) J(L096) J(L097)
		J(L098) J(L099) J(L09a) J(L09b) J(L09c) J(L09d) J(L09e) J(L09f)
		J(L0a0) J(L0a1) J(L0a2) J(L0a3) J(L0a4) J(L0a5) J(L0a6) J(L0a7)
		J(L0a8) J(L0a9) J(L0aa) J(L0ab) J(L0ac) J(L0ad) J(L0ae) J(L0af)
		J(L0b0) J(L0b1) J(L0b2) J(L0b3) J(L0b4) J(L0b5) J(L0b6) J(L0b7)
		J(L0b8) J(L0b9) J(L0ba) J(L0bb) J(L0bc) J(L0bd) J(L0be) J(L0bf)

		J(L0c0) J(L0c1) J(L0c2) J(L0c3) J(L0c4) J(L0c5) J(L0c6) J(L0c7)
		J(L0c8) J(L0c9) J(L0ca) J(L0cb) J(L0cc) J(L0cd) J(L0ce) J(L0cf)
		J(L0d0) J(L0d1) J(L0d2) J(L0d3) J(L0d4) J(L0d5) J(L0d6) J(L0d7)
		J(L0d8) J(L0d9) J(L0da) J(L0db) J(L0dc) J(L0dd) J(L0de) J(L0df)
		J(L0e0) J(L0e1) J(L0e2) J(L0e3) J(L0e4) J(L0e5) J(L0e6) J(L0e7)
		J(L0e8) J(L0e9) J(L0ea) J(L0eb) J(L0ec) J(L0ed) J(L0ee) J(L0ef)
		J(L0f0) J(L0f1) J(L0f2) J(L0f3) J(L0f4) J(L0f5) J(L0f6) J(L0f7)
		J(L0f8) J(L0f9) J(L0fa) J(L0fb) J(L0fc) J(L0fd) J(L0fe) J(L0ff)

		J(L100) J(L101) J(L102) J(L103) J(L104) J(L105) J(L106) J(L107)
		J(L108) J(L109) J(L10a) J(L10b) J(L10c) J(L10d) J(L10e) J(L10f)
		J(L110) J(L111) J(L112) J(L113) J(L114) J(L115) J(L116) J(L117)
		J(L118) J(L119) J(L11a) J(L11b) J(L11c) J(L11d) J(L11e) J(L11f)
		J(L120) J(L121) J(L122) J(L123) J(L124) J(L125) J(L126) J(L127)
		J(L128) J(L129) J(L12a) J(L12b) J(L12c) J(L12d) J(L12e) J(L12f)
		J(L130) J(L131) J(L132) J(L133) J(L134) J(L135) J(L136) J(L137)
		J(L138) J(L139) J(L13a) J(L13b) J(L13c) J(L13d) J(L13e) J(L13f)

		J(L140) J(L141) J(L142) J(L143) J(L144) J(L145) J(L146) J(L147)
		J(L148) J(L149) J(L14a) J(L14b) J(L14c) J(L14d) J(L14e) J(L14f)
		J(L150) J(L151) J(L152) J(L153) J(L154) J(L155) J(L156) J(L157)
		J(L158) J(L159) J(L15a) J(L15b) J(L15c) J(L15d) J(L15e) J(L15f)
		J(L160) J(L161) J(L162) J(L163) J(L164) J(L165) J(L166) J(L167)
		J(L168) J(L169) J(L16a) J(L16b) J(L16c) J(L16d) J(L16e) J(L16f)
		J(L170) J(L171) J(L172) J(L173) J(L174) J(L175) J(L176) J(L177)
		J(L178) J(L179) J(L17a) J(L17b) J(L17c) J(L17d) J(L17e) J(L17f)

		J(L180) J(L181) J(L182) J(L183) J(L184) J(L185) J(L186) J(L187)
		J(L188) J(L189) J(L18a) J(L18b) J(L18c) J(L18d) J(L18e) J(L18f)
		J(L190) J(L191) J(L192) J(L193) J(L194) J(L195) J(L196) J(L197)
		J(L198) J(L199) J(L19a) J(L19b) J(L19c) J(L19d) J(L19e) J(L19f)
		J(L1a0) J(L1a1) J(L1a2) J(L1a3) J(L1a4) J(L1a5) J(L1a6) J(L1a7)
		J(L1a8) J(L1a9) J(L1aa) J(L1ab) J(L1ac) J(L1ad) J(L1ae) J(L1af)
		J(L1b0) J(L1b1) J(L1b2) J(L1b3) J(L1b4) J(L1b5) J(L1b6) J(L1b7)
		J(L1b8) J(L1b9) J(L1ba) J(L1bb) J(L1bc) J(L1bd) J(L1be) J(L1bf)

		J(L1c0) J(L1c1) J(L1c2) J(L1c3) J(L1c4) J(L1c5) J(L1c6) J(L1c7)
		J(L1c8) J(L1c9) J(L1ca) J(L1cb) J(L1cc) J(L1cd) J(L1ce) J(L1cf)
		J(L1d0) J(L1d1) J(L1d2) J(L1d3) J(L1d4) J(L1d5) J(L1d6) J(L1d7)
		J(L1d8) J(L1d9) J(L1da) J(L1db) J(L1dc) J(L1dd) J(L1de) J(L1df)
		J(L1e0) J(L1e1) J(L1e2) J(L1e3) J(L1e4) J(L1e5) J(L1e6) J(L1e7)
		J(L1e8) J(L1e9) J(L1ea) J(L1eb) J(L1ec) J(L1ed) J(L1ee) J(L1ef)
		J(L1f0) J(L1f1) J(L1f2) J(L1f3) J(L1f4) J(L1f5) J(L1f6) J(L1f7)
		J(L1f8) J(L1f9) J(L1fa) J(L1fb) J(L1fc) J(L1fd) J(L1fe) J(L1ff)
	}
ret:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

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
