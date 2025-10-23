/*
 * Copyright (C) 2014-2020 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King.
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
#include "stress-ng.h"
#include "core-arch.h"
#include "core-asm-x86.h"
#include "core-builtin.h"
#include "core-cpu.h"

#if defined(XMMINTRIN_H)
#include <ximmintrin.h>
#endif

#define X86_FP_DAZ		(0x0040UL)
#define X86_FP_FTZ		(0x8000UL)

	/* Name + dest reg */			/* Input -> Output */
#define CPUID_sse3_ECX		(1U << 0)	/* EAX=0x1 -> ECX */
#define CPUID_pclmulqdq_ECX	(1U << 1)	/* EAX=0x1 -> ECX */
#define CPUID_dtes64_ECX	(1U << 2)	/* EAX=0x1 -> ECX */
#define CPUID_monitor_ECX	(1U << 3)	/* EAX=0x1 -> ECX */
#define CPUID_ds_cpl_ECX	(1U << 4)	/* EAX=0x1 -> ECX */
#define CPUID_vmx_ECX		(1U << 5)	/* EAX=0x1 -> ECX */
#define CPUID_smx_ECX		(1U << 6)	/* EAX=0x1 -> ECX */
#define CPUID_est_ECX		(1U << 7)	/* EAX=0x1 -> ECX */
#define CPUID_tm2_ECX		(1U << 8)	/* EAX=0x1 -> ECX */
#define CPUID_ssse3_ECX		(1U << 9)	/* EAX=0x1 -> ECX */
#define CPUID_cnxt_id_ECX	(1U << 10)	/* EAX=0x1 -> ECX */
#define CPUID_sdbg_ECX		(1U << 11)	/* EAX=0x1 -> ECX */
#define CPUID_fma_ECX		(1U << 12)	/* EAX=0x1 -> ECX */
#define CPUID_cx16_ECX		(1U << 13)	/* EAX=0x1 -> ECX */
#define CPUID_xtpr_ECX		(1U << 14)	/* EAX=0x1 -> ECX */
#define CPUID_pdcm_ECX		(1U << 15)	/* EAX=0x1 -> ECX */
#define CPUID_pcid_ECX		(1U << 17)	/* EAX=0x1 -> ECX */
#define CPUID_dca_ECX		(1U << 18)	/* EAX=0x1 -> ECX */
#define CPUID_sse4_1_ECX	(1U << 19)	/* EAX=0x1 -> ECX */
#define CPUID_sse4_2_ECX	(1U << 20)	/* EAX=0x1 -> ECX */
#define CPUID_x2apic_ECX	(1U << 21)	/* EAX=0x1 -> ECX */
#define CPUID_movbe_ECX		(1U << 22)	/* EAX=0x1 -> ECX */
#define CPUID_popcnt_ECX	(1U << 23)	/* EAX=0x1 -> ECX */
#define CPUID_tsc_deadline_ECX	(1U << 24)	/* EAX=0x1 -> ECX */
#define CPUID_aes_ECX		(1U << 25)	/* EAX=0x1 -> ECX */
#define CPUID_xsave_ECX		(1U << 26)	/* EAX=0x1 -> ECX */
#define CPUID_osxsave_ECX	(1U << 27)	/* EAX=0x1 -> ECX */
#define CPUID_avx_ECX		(1U << 28)	/* EAX=0x1 -> ECX */
#define CPUID_f16c_ECX		(1U << 29)	/* EAX=0x1 -> ECX */
#define CPUID_rdrnd_ECX		(1U << 30)	/* EAX=0x1 -> ECX */

#define CPUID_fpu_EDX		(1U << 0)	/* EAX=0x1 -> EDX */
#define CPUID_vme_EDX		(1U << 1)	/* EAX=0x1 -> EDX */
#define CPUID_de_EDX		(1U << 2)	/* EAX=0x1 -> EDX */
#define CPUID_pse_EDX		(1U << 3)	/* EAX=0x1 -> EDX */
#define CPUID_tsc_EDX		(1U << 4)	/* EAX=0x1 -> EDX */
#define CPUID_msr_EDX		(1U << 5)	/* EAX=0x1 -> EDX */
#define CPUID_pae_EDX		(1U << 6)	/* EAX=0x1 -> EDX */
#define CPUID_mce_EDX		(1U << 7)	/* EAX=0x1 -> EDX */
#define CPUID_cx8_EDX		(1U << 8)	/* EAX=0x1 -> EDX */
#define CPUID_apic_EDX		(1U << 9)	/* EAX=0x1 -> EDX */
#define CPUID_sep_EDX		(1U << 11)	/* EAX=0x1 -> EDX */
#define CPUID_mtrr_EDX		(1U << 12)	/* EAX=0x1 -> EDX */
#define CPUID_pge_EDX		(1U << 13)	/* EAX=0x1 -> EDX */
#define CPUID_mca_EDX		(1U << 14)	/* EAX=0x1 -> EDX */
#define CPUID_cmov_EDX		(1U << 15)	/* EAX=0x1 -> EDX */
#define CPUID_pat_EDX		(1U << 16)	/* EAX=0x1 -> EDX */
#define CPUID_pse_36_EDX	(1U << 17)	/* EAX=0x1 -> EDX */
#define CPUID_psn_EDX		(1U << 18)	/* EAX=0x1 -> EDX */
#define CPUID_clfsh_EDX		(1U << 19)	/* EAX=0x1 -> EDX */
#define CPUID_ds_EDX		(1U << 21)	/* EAX=0x1 -> EDX */
#define CPUID_acpi_EDX		(1U << 22)	/* EAX=0x1 -> EDX */
#define CPUID_mmx_EDX		(1U << 23)	/* EAX=0x1 -> EDX */
#define CPUID_fxsr_EDX		(1U << 24)	/* EAX=0x1 -> EDX */
#define CPUID_sse_EDX		(1U << 25)	/* EAX=0x1 -> EDX */
#define CPUID_sse2_EDX		(1U << 26)	/* EAX=0x1 -> EDX */
#define CPUID_ss_EDX		(1U << 27)	/* EAX=0x1 -> EDX */
#define CPUID_htt_EDX		(1U << 28)	/* EAX=0x1 -> EDX */
#define CPUID_tm_EDX		(1U << 29)	/* EAX=0x1 -> EDX */
#define CPUID_ia64_EDX		(1U << 30)	/* EAX=0x1 -> EDX */
#define CPUID_pbe_EDX		(1U << 31)	/* EAX=0x1 -> EDX */

#define CPUID_fsgsbase_EBX	(1U << 0)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_sgx_EBX 		(1U << 2)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_bmi1_EBX		(1U << 3)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_hle_EBX 		(1U << 4)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_avx2_EBX		(1U << 5)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_smep_EBX 		(1U << 7)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_bmi2_EBX 		(1U << 8)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_erms_EBX 		(1U << 9)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_invpcid_EBX 	(1U << 10)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_rtm_EBX 		(1U << 11)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_pqm_EBX 		(1U << 12)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_mpx_EBX 		(1U << 14)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_pqe_EBX 		(1U << 15)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_avx512_f_EBX	(1U << 16)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_avx512_dq_EBX 	(1U << 17)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_rdseed_EBX	(1U << 18)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_adx_EBX 		(1U << 19)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_smap_EBX 		(1U << 20)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_avx512_ifma_EBX	(1U << 21)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_pcommit_EBX 	(1U << 22)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_clflushopt_EBX 	(1U << 23)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_clwb_EBX		(1U << 24)	/* EAX=0x7, ECX=0x0 -> EBX */
#define CPUID_intel_pt_EBX	(1U << 25)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_avx512_pf_EBX	(1U << 26)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_avx512_er_EBX	(1U << 27)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_avx512_cd_EBX	(1U << 28)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_sha_EBX		(1U << 29)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_avx512_bw_EBX	(1U << 30)	/* EAX=0x7, EXC=0x0 -> EBX */
#define CPUID_avx512_vl_EBX	(1U << 31)	/* EAX=0x7, EXC=0x0 -> EBX */

#define CPUID_prefetchwt1_ECX	(1U << 0)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_avx512_vbmi_ECX	(1U << 1)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_umip_ECX		(1U << 2)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_pku_ECX		(1U << 3)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_ospke_ECX		(1U << 4)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_waitpkg_ECX	(1U << 5)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_avx512_vbmi2_ECX	(1U << 6)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_cet_ss_ECX	(1U << 7)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_gfni_ECX		(1U << 8)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_vaes_ECX		(1U << 9)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_vclmulqdq_ECX	(1U << 10)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_avx512_vnni_ECX	(1U << 11)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_avx512_bitalg_ECX	(1U << 12)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_tme_en_ECX	(1U << 13)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_avx512_vpopcntdq_ECX (1U << 14)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_rdpid_ECX		(1U << 22)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_kl_ECX		(1U << 23)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_cldemote_ECX	(1U << 25)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_movdiri_ECX	(1U << 27)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_movdir64b_ECX	(1U << 28)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_enqcmd_ECX	(1U << 29)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_sgx_lc_ECX	(1U << 30)	/* EAX=0x7, ECX=0x0, -> ECX */
#define CPUID_pks_ECX		(1U << 31)	/* EAX=0x7, ECX=0x0, -> ECX */

#define CPUID_avx512_4vnniw_EDX	(1U << 2)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_avx512_4fmaps_EDX	(1U << 3)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_fsrm_EDX		(1U << 4)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_avx512_vp2intersect_EDX (1U << 8)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_srbds_ctrl_edx	(1U << 9)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_md_clear_EDX	(1U << 10)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_tsx_force_abort_EDX (1U << 13)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_serialize_EDX	(1U << 14)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_hybrid_EDX	(1U << 15)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_tsxldtrk_EDX	(1U << 16)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_pconfig_EDX	(1U << 18)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_lbr_EDX		(1U << 19)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_cet_ibt_EDX	(1U << 20)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_amx_bf16_EDX	(1U << 22)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_avx512_fp16_EDX	(1U << 23)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_amx_tile_EDX	(1U << 24)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_amx_int8_EDX	(1U << 25)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_ibrs_ibpb_EDX	(1U << 26)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_stip_EDX		(1U << 27)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_l1d_flush_EDX	(1U << 28)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_ia32_arch_cap_EDX	(1U << 29)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_ia32_core_cap_EDX	(1U << 30)	/* EAX=0x7, ECX=0x0, -> EDX */
#define CPUID_ssbd_EDX		(1U << 31)	/* EAX=0x7, ECX=0x0, -> EDX */

#define CPUID_sh512_EAX		(1U << 0)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_sm3_EAX		(1U << 1)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_sm4_EAX		(1U << 2)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_raio_int_EAX	(1U << 3)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_avx_vnni_EAX	(1U << 4)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_avx512_bf16_EAX	(1U << 5)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_lass_EAX		(1U << 6)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_compccxadd_EAX	(1U << 7)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_archperfmonext_EAX (1U << 8)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_fast_zero_rep_movsb_EAX (1U << 10) /* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_fast_short_rep_stosb_EAX (1U << 11) /* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_fast_short_rep_cmpsb_scasb_EAX (1U << 12)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_fred_EAX		(1U << 17)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_lkgs_EAX		(1U << 18)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_wrmsrns_EAX	(1U << 19)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_amx_fp16_EAX	(1U << 21)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_hreset_EAX	(1U << 22)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_avx_ifma_EAX	(1U << 23)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_lam_EAX		(1U << 26)	/* EAX=0x7, ECX=0x1, -> EAX */
#define CPUID_msrlist_EAX	(1U << 27)	/* EAX=0x7, ECX=0x1, -> EAX */

#define CPUID_tse_EBX		(1U << 1)	/* EAX=0x7, ECX=0x1, -> EBX */

#define CPUID_avx_vnni_int8_EDX	(1U << 4)	/* EAX=0x7, ECX=0x1, -> EDX */
#define CPUID_avx_ne_convert_EDX (1U << 5)	/* EAX=0x7, ECX=0x1, -> EDX */
#define CPUID_amx_complex_EDX	(1U << 8)	/* EAX=0x7, ECX=0x1, -> EDX */
#define CPUID_amx_vnni_int16_EDX (1U << 10)	/* EAX=0x7, ECX=0x1, -> EDX */
#define CPUID_prefetchi_EDX	(1U << 14)	/* EAX=0x7, ECX=0x1, -> EDX */
#define CPUID_uiret_uiif_from_rflags_EDX (1U << 17) /* EAX=0x7, ECX=0x1, -> EDX */
#define CPUID_cet_sss_EDX	(1U << 18)	/* EAX=0x7, ECX=0x1, -> EDX */
#define CPUID_avx10_EDX		(1U << 19)	/* EAX=0x7, ECX=0x1, -> EDX */
#define CPUID_apx_f_EDX		(1U << 21)	/* EAX=0x7, ECX=0x1, -> EDX */

#define CPUID_pfsd_EDX		(1U << 0)	/* EAX=0x7, ECX=0x2, -> EDX */
#define CPUID_ipred_dis_EDX	(1U << 1)	/* EAX=0x7, ECX=0x2, -> EDX */
#define CPUID_rrsba_ctrl_EDX	(1U << 2)	/* EAX=0x7, ECX=0x2, -> EDX */
#define CPUID_dppd_u_EDX	(1U << 3)	/* EAX=0x7, ECX=0x2, -> EDX */
#define CPUID_bhi_ctrl_EDX	(1U << 4)	/* EAX=0x7, ECX=0x2, -> EDX */
#define CPUID_mcdt_no_EDX	(1U << 5)	/* EAX=0x7, ECX=0x2, -> EDX */

#define CPUID_amd_fpu_EDX	(1U << 0)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_vme_EDX	(1U << 1)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_de_EDX	(1U << 2)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_pse_EDX	(1U << 3)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_tsc_EDX	(1U << 4)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_msr_EDX	(1U << 5)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_pae_EDX	(1U << 6)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_mce_EDX	(1U << 7)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_cx8_EDX	(1U << 8)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_apic_EDX	(1U << 9)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_syscall_k6_EDX (1U << 10)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_syscall_EDX	(1U << 11)	/* EAX=0x80000001 -> EDX */
#define CPUID_amd_mtrr_EDX	(1U << 12)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_pge_EDX	(1U << 13)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_mca_EDX	(1U << 14)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_cmov_EDX	(1U << 15)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_pat_EDX	(1U << 16)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_pse36_EDX	(1U << 17)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_ecc_EDX	(1U << 19)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_nx_EDX	(1U << 20)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_mmxext_EDX	(1U << 22)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_mmx_EDX	(1U << 23)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_fxsr_EDX	(1U << 24)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_fxsr_opt_EDX	(1U << 25)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_pdpe1ge_EDX	(1U << 26)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_rdtscp_EDX	(1U << 27)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_lm_EDX	(1U << 29)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_3dnowext_EDX	(1U << 30)	/* EAX=0800000001 -> EDX */
#define CPUID_amd_3dnow_EDX	(1U << 31)	/* EAX=0800000001 -> EDX */

#define CPUID_amd_lahf_lm_ECX	(1U << 0)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_cmp_legacy_ECX (1U << 1)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_svm_ECX	(1U << 2)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_extapic_ECX	(1U << 3)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_cr8_legacy_ECX (1U << 4)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_abm_lzcnt_ECX	(1U << 5)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_sse4a_ECX	(1U << 6)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_misalignsse_ECX (1U << 7)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_3dnowprefetch_ECX (1U << 8)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_osvw_ECX	(1U << 9)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_ibs_ECX	(1U << 10)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_xop_ECX	(1U << 11)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_skinit_ECX	(1U << 12)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_wdt_ECX	(1U << 13)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_lwp_ECX	(1U << 15)	/* EAX=0800000001 -> ECX */

#define CPUID_amd_fma4_ECX	(1U << 16)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_tce_ECX	(1U << 17)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_nodeid_msr_ECX (1U << 19)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_tbm_ECX	(1U << 21)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_topoext_ECX	(1U << 22)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_perfctr_core_ECX (1U << 23)	/* EAX=0800000001 -> ECX */

#define CPUID_amd_perfctr_nb_ECX (1U << 24)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_streamperfmon_ECX (1U << 25)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_dbx_ECX	(1U << 26)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_perftsc_ECX	(1U << 27)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_pcxl2i_ECX	(1U << 28)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_monitorx_ECX	(1U << 29)	/* EAX=0800000001 -> ECX */
#define CPUID_amd_addr_mask_ext_ECX (1U << 30)	/* EAX=0800000001 -> ECX */

/*
 *  stress_cpu_is_x86()
 *	Intel x86 test
 */
bool stress_cpu_is_x86(void)
{
#if defined(STRESS_ARCH_X86)
	/*
	 *  Kudos to https://en.wikipedia.org/wiki/CPUID
	 */
	static const char * const x86_id_str[] = {
		"AMD ISBETTER",		/* early engineering samples of AMD K5 processor */
		"AMDisbetter!",		/* early engineering samples of AMD K5 processor */
		"AuthenticAMD",		/* AMD */
		"CentaurHauls",		/* IDT WinChip/Centaur (Including some VIA and Zhaoxin CPUs) */
		"Compaq FX!32",		/* Compaq FX!32 */
		"ConnectixCPU",		/* Connectix Virtual PC (version 6 and lower) */
		"CyrixInstead",		/* Cyrix/early STMicroelectronics and IBM */
		"E2K MACHINE\0",	/* MCST Elbrus */
		"Genuine  RDC",		/* RDC Semiconductor Co. Ltd. */
		"GenuineAO486",		/* ao486 CPU (old) */
		"GenuineIntel",		/* Intel */
		"GenuineIotel",		/* Intel (https://twitter.com/InstLatX64/status/1101230794364862464) */
		"GenuineTMx86",		/* Transmeta */
		"Geode by NSC",		/* National Semiconductor */
		"HygonGenuine",		/* Hygon */
		"Insignia 586",		/* Insignia RealPC and SoftWindows 98 */
		"MicrosoftXTA",		/* Microsoft x86-to-ARM */
		"MiSTer AO486",		/* ao486 CPU */
		"Neko Project",		/* Neko Project II (PC-98 emulator) */
		"NexGenDriven",		/* NexGen */
		"PowerVM Lx86",		/* PowerVM Lx86 (x86 emulator for IBM POWER5/POWER6) */
		"RiseRiseRise",		/* Rise */
		"SiS SiS SiS ",		/* SiS */
		"TransmetaCPU",		/* Transmeta */
		"UMC UMC UMC ",		/* UMC */
		"VIA VIA VIA ",		/* VIA */
		"VirtualApple",		/* Newer versions of Apple Rosetta 2 */
		"Virtual CPU ",		/* Microsoft Virtual PC 7 */
		"Vortex86 SoC",		/* DM&P Vortex86 */
		"  Shanghai  ",		/* Zhaoxin */
	};

	/* Virtual CPUs */
	static const char * const x86_virt_id_str[] = {
		"___ NVMM ___",		/* NetBSD NVMM */
		" lrpepyh  vr",		/* Parallels */
		" QNXQVMBSQG ",		/* QNX Hypervisor */
		"ACRNACRNACRN",		/* Project ACRN */
		"bhyve bhyve ",		/* bhyve VM */
		"BHyVE BHyVE",		/* bhyve VM */
		"EVMMEVMMEVMM",		/* Intel KGT (Trusty) */
		"FEXIFEXIEMU\0",	/* FEX-Emu */
		"HAXMHAXMHAXM",		/* Intel HAXM */
		"Jailhouse\0\0\0",	/* Jailhouse */
		"KVMKVMKVM\0\0\0",	/* Linux KVM */
		"Linux KVM Hv",		/* Linux KVM Hyper-V emulation */
		"Microsoft Hv",		/* Microsoft Hyper-V or Windows Virtual PC */
		"Napocahv    ",		/* Bitdefender Napoca */
		"OpenBSDVMM58",		/* OpenBSD VMM */
		"prl hyperv  ",		/* Parallels */
		"SRESRESRESRE",		/* Lockheed Martin LMHS */
		"TCGTCGTCGTCG",		/* QEMU */
		"UnisysSpar64",		/* Unisys s-Par */
		"VBoxVBoxVBox",		/* VirtualBox */
		"VMwareVMware",		/* VMWare */
		"XenVMMXenVMM",		/* XEN HVM */
	};

	uint32_t eax, ebx, ecx, edx;
	size_t i;

	eax = 0, ebx = 0, ecx = 0, edx = 0;
	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	/* Intel CPU? */
	for (i = 0; i < SIZEOF_ARRAY(x86_id_str); i++) {
		const char *str = x86_id_str[i];

		if ((shim_memcmp(&ebx, str + 0, 4) == 0) &&
		    (shim_memcmp(&edx, str + 4, 4) == 0) &&
		    (shim_memcmp(&ecx, str + 8, 4) == 0))
		return true;
	}

	/* Virtual machine? */
	eax = 0x40000000, ebx = 0, ecx = 0, edx = 0;
	stress_asm_x86_cpuid(eax, ebx, ecx, edx);
	for (i = 0; i < SIZEOF_ARRAY(x86_virt_id_str); i++) {
		const char *str = x86_virt_id_str[i];

		if ((shim_memcmp(&ebx, str + 0, 4) == 0) &&
		    (shim_memcmp(&edx, str + 4, 4) == 0) &&
		    (shim_memcmp(&ecx, str + 8, 4) == 0))
		return true;
	}
#endif
	return false;
}

/*
 *  stress_cpu_x86_extended_features
 *	cpuid EAX=7, ECX=0: Extended Features
 */
#if defined(STRESS_ARCH_X86)
#define stress_cpu_x86_extended_features(ebx, ecx, edx)	\
{							\
	uint32_t eax = 7;				\
							\
	stress_asm_x86_cpuid(eax, ebx, ecx, edx);	\
}
#endif

/*
 *  stress_cpu_x86_has_clflushopt()
 *	does x86 cpu support clflushopt?
 */
bool stress_cpu_x86_has_clflushopt(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_cpu_x86_extended_features(ebx, ecx, edx);

	return !!(ebx & CPUID_clflushopt_EBX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_clwb()
 *	does x86 cpu support clwb?
 */
bool stress_cpu_x86_has_clwb(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_cpu_x86_extended_features(ebx, ecx, edx);

	return !!(ebx & CPUID_clwb_EBX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_cldemote()
 *	does x86 cpu support cldemote?
 */
bool stress_cpu_x86_has_cldemote(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_cpu_x86_extended_features(ebx, ecx, edx);

	return !!(ecx & CPUID_cldemote_ECX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_prefetchwt1()
 *	does x86 cpu support prefetchwt1?
 */
bool stress_cpu_x86_has_prefetchwt1(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_cpu_x86_extended_features(ebx, ecx, edx);

	return !!(ecx & CPUID_prefetchwt1_ECX);
#else
	return false;
#endif
}


/*
 *  stress_cpu_x86_has_waitpkg()
 *	does x86 cpu support waitpkg?
 */
bool stress_cpu_x86_has_waitpkg(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_cpu_x86_extended_features(ebx, ecx, edx);

	return !!(ecx & CPUID_waitpkg_ECX);
#else
	return false;
#endif
}


/*
 *  stress_cpu_x86_has_rdseed()
 *	does x86 cpu support rdseed?
 */
bool stress_cpu_x86_has_rdseed(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_cpu_x86_extended_features(ebx, ecx, edx);

	return !!(ebx & CPUID_rdseed_EBX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_syscall()
 *	does x86 cpu support syscall?
 */
bool stress_cpu_x86_has_syscall(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x80000001, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(edx & CPUID_amd_syscall_EDX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_lahf_lm()
 *	does x86 cpu support LAHFSAHF in long mode?
 */
bool stress_cpu_x86_has_lahf_lm(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x80000001, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(ecx & CPUID_amd_lahf_lm_ECX);
#else
	return false;
#endif
}


/*
 *  stress_cpu_x86_has_rdrand()
 *	does x86 cpu support rdrand?
 */
bool stress_cpu_x86_has_rdrand(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x1, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(ecx & CPUID_rdrnd_ECX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_tsc()
 *	does x86 cpu support tsc?
 */
bool stress_cpu_x86_has_tsc(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x1, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(edx & CPUID_tsc_EDX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_rdtscp()
 *	does x86 cpu support rdtscp?
 */
bool stress_cpu_x86_has_rdtscp(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x80000001, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(edx & CPUID_amd_rdtscp_EDX);
#else
	return false;
#endif
}


/*
 *  stress_cpu_x86_has_msr()
 *	does x86 cpu support MSRs?
 */
bool stress_cpu_x86_has_msr(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x1, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(edx & CPUID_msr_EDX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_clfsh()
 *	does x86 cpu support clflush?
 */
bool stress_cpu_x86_has_clfsh(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x1, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(edx & CPUID_clfsh_EDX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_mmx()
 *	does x86 cpu support mmx?
 */
bool stress_cpu_x86_has_mmx(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x1, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(edx & CPUID_mmx_EDX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_sse()
 *	does x86 cpu support sse?
 */
bool stress_cpu_x86_has_sse(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x1, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(edx & CPUID_sse_EDX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_sse2()
 *	does x86 cpu support sse?
 */
bool stress_cpu_x86_has_sse2(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x1, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(edx & CPUID_sse2_EDX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_serialize()
 *	does x86 cpu support serialize opcode?
 */
bool stress_cpu_x86_has_serialize(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x7, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(edx & CPUID_serialize_EDX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_avx_vnni()
 *	does x86 cpu support avx_vnni
 */
bool stress_cpu_x86_has_avx_vnni(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x7, ebx = 0, ecx = 1, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(eax & CPUID_avx_vnni_EAX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_avx512_vl()
 *	does x86 cpu support avx512_vl
 */
bool stress_cpu_x86_has_avx512_vl(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x7, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(ebx & CPUID_avx512_vl_EBX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_avx512_vnni()
 *	does x86 cpu support avx512_vnni
 */
bool stress_cpu_x86_has_avx512_vnni(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x7, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(ecx & CPUID_avx512_vnni_ECX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_x86_has_avx512_bw()
 *	does x86 cpu support avx512_bw
 */
bool stress_cpu_x86_has_avx512_bw(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x7, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(ebx & CPUID_avx512_bw_EBX);
#else
	return false;
#endif
}

/*
 *  stress_cpu_disable_fp_subnormals
 *     Floating Point subnormals can be expensive and require
 *     micro-ops from the Microcode Sequencer ROM. Disabling
 *     these makes FP ops faster but not strictly IEEE compliant.
 *     See https://en.wikipedia.org/wiki/Subnormal_number
 */
void stress_cpu_disable_fp_subnormals(void)
{
#if defined(STRESS_ARCH_X86) &&		\
    defined(HAVE_IMMINTRIN_H) &&	\
    defined(HAVE_MM_GETCSR) &&		\
    defined(HAVE_MM_SETCSR)
	if (stress_cpu_x86_has_sse())
		_mm_setcsr(_mm_getcsr() | (X86_FP_DAZ | X86_FP_FTZ));
#endif
}

/*
 *  stress_cpu_enable_fp_subnormals
 *     Floating Point subnormals can be expensive and require
 *     micro-ops from the Microcode Sequencer ROM. Enable them to
 *     be IEEE compliant and slower.
 */
void stress_cpu_enable_fp_subnormals(void)
{
#if defined(STRESS_ARCH_X86) &&		\
    defined(HAVE_IMMINTRIN_H) &&	\
    defined(HAVE_MM_GETCSR) &&		\
    defined(HAVE_MM_SETCSR)
	if (stress_cpu_x86_has_sse())
		_mm_setcsr(_mm_getcsr() & ~(X86_FP_DAZ | X86_FP_FTZ));
#endif
}

/*
 *  stress_cpu_x86_has_movdiri()
 *	does x86 cpu support movdiri
 */
bool stress_cpu_x86_has_movdiri(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x7, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_asm_x86_cpuid(eax, ebx, ecx, edx);

	return !!(ecx & CPUID_movdiri_ECX);
#else
	return false;
#endif
}

