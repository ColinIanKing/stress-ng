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
	{ NULL,	"ipsec-mb N",	  "start N workers exercising the IPSEC MB encoding" },
	{ NULL,	"ipsec-mb-ops N", "stop after N ipsec bogo encoding operations" },
	{ NULL,	NULL,		  NULL }
};

#if defined(HAVE_INTEL_IPSEC_MB_H) &&	\
    defined(HAVE_LIB_IPSEC_MB) &&	\
    defined(STRESS_X86) &&		\
    (defined(__x86_64__) || defined(__x86_64)) && \
    defined(IMB_FEATURE_SSE4_2) &&	\
    defined(IMB_FEATURE_CMOV) &&	\
    defined(IMB_FEATURE_AESNI) &&	\
    defined(IMB_FEATURE_AVX) &&		\
    defined(IMB_FEATURE_AVX2) &&	\
    defined(IMB_FEATURE_AVX512_SKX) 

#define FEATURE_SSE		(IMB_FEATURE_SSE4_2 | IMB_FEATURE_CMOV | IMB_FEATURE_AESNI)
#define FEATURE_AVX		(IMB_FEATURE_AVX | IMB_FEATURE_CMOV | IMB_FEATURE_AESNI)
#define FEATURE_AVX2		(FEATURE_AVX | IMB_FEATURE_AVX2)
#define FEATURE_AVX512		(FEATURE_AVX2 | IMB_FEATURE_AVX512_SKX)

/*
 *  stress_ipsec_mb_features()
 *	get list of CPU feature bits
 */
static int stress_ipsec_mb_features(const args_t *args, MB_MGR *p_mgr)
{
	int features;

	features = p_mgr->features;

	if (args->instance == 0) {
		char str[128] = "";

		if ((features & FEATURE_SSE) == FEATURE_SSE)
			strcat(str, " sse");
		if ((features & FEATURE_AVX) == FEATURE_AVX)
			strcat(str, " avx");
		if ((features & FEATURE_AVX2) == FEATURE_AVX2)
			strcat(str, " avx2");
		if ((features & FEATURE_AVX512) == FEATURE_AVX512)
			strcat(str, " avx512");

		pr_inf("%s: features:%s\n", args->name, str);
	}
	return features;
}

/*
 *  stress_ipsec_mb_supported()
 *	check if ipsec_mb is supported
 */
static int stress_ipsec_mb_supported(void)
{
	/* Intel CPU? */
	if (!cpu_is_x86()) {
		pr_inf("ipsec_mb stressor will be skipped, "
			"not a recognised Intel CPU\n");
		return -1;
	}
	return 0;
}

/*
 *  stress_rnd_fill()
 *	fill uint32_t aligned buf with n bytes of random data
 */
static void stress_rnd_fill(uint8_t *buf, size_t n)
{
	register uint32_t *ptr = (uint32_t *)buf;
	register uint32_t *end = (uint32_t *)(buf + n);

	while (ptr < end)
		*(ptr++) = mwc32();
}

/*
 *  stress_job_empty()
 *	empty job queue
 */
static inline void stress_job_empty(struct MB_MGR *mb_mgr)
{
	while (IMB_FLUSH_JOB(mb_mgr))
		;
}

static void stress_sha(
	const args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs)
{
	int j;
	const int sha_digest_size = 64;
	struct JOB_AES_HMAC *job;
	uint8_t digest[jobs * sha_digest_size];

	stress_rnd_fill(digest, sizeof(digest));

	stress_job_empty(mb_mgr);

	for (j = 0; j < jobs; j++) {
		uint8_t *auth = digest + (j * sha_digest_size);

		job = IMB_GET_NEXT_JOB(mb_mgr);
		(void)memset(job, 0, sizeof(*job));

		job->cipher_direction = ENCRYPT;
		job->chain_order = HASH_CIPHER;
		job->auth_tag_output = auth;
		job->auth_tag_output_len_in_bytes = sha_digest_size;
		job->src = data;
		job->msg_len_to_hash_in_bytes = data_len;
		job->cipher_mode = NULL_CIPHER;
		job->hash_alg = PLAIN_SHA_512;
		job = IMB_SUBMIT_JOB(mb_mgr);
		(void)job;
	}

	for (j = 0; j < jobs; j++) {
		job = IMB_FLUSH_JOB(mb_mgr);
		if (!job)
			break;
		if (job->status != STS_COMPLETED) {
			pr_err("%s: sha: job %d not completed\n",
				args->name, j);
		}
	}
	if (j != jobs)
		pr_err("%s: sha: only processed %d of %d jobs\n",
			args->name, j, jobs);
}

static void stress_des(
	const args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs)
{
	int j;
	struct JOB_AES_HMAC *job;

	uint8_t encoded[jobs * data_len] ALIGNED(16);
	uint8_t k[32] ALIGNED(16);
	uint8_t iv[16] ALIGNED(16);
	uint32_t enc_keys[15*4] ALIGNED(16);
	uint32_t dec_keys[15*4] ALIGNED(16);

	stress_rnd_fill(k, sizeof(k));
	stress_rnd_fill(iv, sizeof(iv));
	stress_job_empty(mb_mgr);
	IMB_AES_KEYEXP_256(mb_mgr, k, enc_keys, dec_keys);

	for (j = 0; j < jobs; j++) {
		uint8_t *dst = encoded + (j * data_len);

		job = IMB_GET_NEXT_JOB(mb_mgr);
		(void)memset(job, 0, sizeof(*job));

		job->cipher_direction = ENCRYPT;
		job->chain_order = CIPHER_HASH;
		job->src = data;
		job->dst = dst;
		job->cipher_mode = CBC;
		job->aes_enc_key_expanded = enc_keys;
		job->aes_dec_key_expanded = dec_keys;
		job->aes_key_len_in_bytes = sizeof(k);
		job->iv = iv;
		job->iv_len_in_bytes = sizeof(iv);
		job->cipher_start_src_offset_in_bytes = 0;
		job->msg_len_to_cipher_in_bytes = data_len;
		job->user_data = dst;
		job->user_data2 = (void *)((uint64_t)j);
		job->hash_alg = NULL_HASH;
		job = IMB_SUBMIT_JOB(mb_mgr);
		(void)job;
	}

	for (j = 0; j < jobs; j++) {
		job = IMB_FLUSH_JOB(mb_mgr);
		if (!job)
			break;
		if (job->status != STS_COMPLETED) {
			pr_err("%s: des: job %d not completed\n",
				args->name, j);
		}
	}
	if (j != jobs)
		pr_err("%s: des: only processed %d of %d jobs\n",
			args->name, j, jobs);
}

static void stress_cmac(
	const args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs)
{
	int j;
	struct JOB_AES_HMAC *job;

	uint8_t key[16] ALIGNED(16);
	uint32_t expkey[4 * 15] ALIGNED(16);
	uint32_t dust[4 * 15] ALIGNED(16);
	uint32_t skey1[4], skey2[4];
	uint8_t output[jobs * data_len] ALIGNED(16);

	stress_rnd_fill(key, sizeof(key));
	IMB_AES_KEYEXP_128(mb_mgr, key, expkey, dust);
	IMB_AES_CMAC_SUBKEY_GEN_128(mb_mgr, expkey, skey1, skey2);
	stress_job_empty(mb_mgr);

	for (j = 0; j < jobs; j++) {
		uint8_t *dst = output + (j * 16);

		job = IMB_GET_NEXT_JOB(mb_mgr);
		(void)memset(job, 0, sizeof(*job));

		job->cipher_direction = ENCRYPT;
		job->chain_order = HASH_CIPHER;
		job->cipher_mode = NULL_CIPHER;
		job->hash_alg = AES_CMAC;
		job->src = data;
		job->hash_start_src_offset_in_bytes = 0;
		job->msg_len_to_hash_in_bytes = data_len;
		job->auth_tag_output = dst;
		job->auth_tag_output_len_in_bytes = 16;
		job->u.CMAC._key_expanded = expkey;
		job->u.CMAC._skey1 = skey1;
		job->u.CMAC._skey2 = skey2;
		job->user_data = dst;
		job = IMB_SUBMIT_JOB(mb_mgr);
		(void)job;
	}

	for (j = 0; j < jobs; j++) {
		job = IMB_FLUSH_JOB(mb_mgr);
		if (!job)
			break;
		if (job->status != STS_COMPLETED) {
			pr_err("%s: cmac: job %d not completed\n",
				args->name, j);
		}
	}
	if (j != jobs)
		pr_err("%s: cmac: only processed %d of %d jobs\n",
			args->name, j, jobs);
}

static void stress_ctr(
	const args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs)
{
	int j;
	struct JOB_AES_HMAC *job;

	uint8_t encoded[jobs * data_len] ALIGNED(16);
	uint8_t key[32] ALIGNED(16);
	uint8_t iv[12] ALIGNED(16);		/* 4 byte nonce + 8 byte IV */
	uint32_t expkey[4 * 15] ALIGNED(16);
	uint32_t dust[4 * 15] ALIGNED(16);

	stress_rnd_fill(key, sizeof(key));
	stress_rnd_fill(iv, sizeof(iv));
	IMB_AES_KEYEXP_256(mb_mgr, key, expkey, dust);
	stress_job_empty(mb_mgr);

	for (j = 0; j < jobs; j++) {
		uint8_t *dst = encoded + (j * data_len);

		job = IMB_GET_NEXT_JOB(mb_mgr);
		(void)memset(job, 0, sizeof(*job));

		job->cipher_direction = ENCRYPT;
		job->chain_order = CIPHER_HASH;
		job->cipher_mode = CNTR;
		job->hash_alg = NULL_HASH;
		job->src = data;
		job->dst = dst;
		job->aes_enc_key_expanded = expkey;
		job->aes_dec_key_expanded = expkey;
		job->aes_key_len_in_bytes = sizeof(key);
		job->iv = iv;
		job->iv_len_in_bytes = sizeof(iv);
		job->cipher_start_src_offset_in_bytes = 0;
		job->msg_len_to_cipher_in_bytes = data_len;
		job = IMB_SUBMIT_JOB(mb_mgr);
		(void)job;
	}

	for (j = 0; j < jobs; j++) {
		job = IMB_FLUSH_JOB(mb_mgr);
		if (!job)
			break;
		if (job->status != STS_COMPLETED) {
			pr_err("%s: ctr: job %d not completed\n",
				args->name, j);
		}
	}
	if (j != jobs)
		pr_err("%s: ctr: only processed %d of %d jobs\n",
			args->name, j, jobs);
}

static void stress_hmac_md5(
	const args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs)
{
	int j;
	size_t i;
	struct JOB_AES_HMAC *job;

	const int digest_size = 16;
	const int block_size = 64;
	uint8_t key[block_size] ALIGNED(16);
	uint8_t buf[block_size] ALIGNED(16);
	uint8_t ipad_hash[digest_size] ALIGNED(16);
	uint8_t opad_hash[digest_size] ALIGNED(16);
	uint8_t output[jobs * digest_size] ALIGNED(16);

	stress_rnd_fill(key, sizeof(key));
	for (i = 0; i < sizeof(key); i++)
		buf[i] = key[i] ^ 0x36;
	IMB_MD5_ONE_BLOCK(mb_mgr, buf, ipad_hash);
	for (i = 0; i < sizeof(key); i++)
		buf[i] = key[i] ^ 0x5c;
	IMB_MD5_ONE_BLOCK(mb_mgr, buf, opad_hash);

	stress_job_empty(mb_mgr);

	for (j = 0; j < jobs; j++) {
		uint8_t *dst = output + (j * digest_size);

		job = IMB_GET_NEXT_JOB(mb_mgr);
		(void)memset(job, 0, sizeof(*job));

		job->aes_enc_key_expanded = NULL;
		job->aes_dec_key_expanded = NULL;
		job->cipher_direction = ENCRYPT;
		job->chain_order = HASH_CIPHER;
		job->dst = NULL;
		job->aes_key_len_in_bytes = 0;
		job->auth_tag_output = dst;
		job->auth_tag_output_len_in_bytes = digest_size;
		job->iv = NULL;
		job->iv_len_in_bytes = 0;
		job->src = data;
		job->cipher_start_src_offset_in_bytes = 0;
		job->msg_len_to_cipher_in_bytes = 0;
		job->hash_start_src_offset_in_bytes = 0;
		job->msg_len_to_hash_in_bytes = data_len;
		job->u.HMAC._hashed_auth_key_xor_ipad = ipad_hash;
		job->u.HMAC._hashed_auth_key_xor_opad = opad_hash;
		job->cipher_mode = NULL_CIPHER;
		job->hash_alg = MD5;
		job->user_data = dst;
		job = IMB_SUBMIT_JOB(mb_mgr);
		(void)job;
	}

	for (j = 0; j < jobs; j++) {
		job = IMB_FLUSH_JOB(mb_mgr);
		if (!job)
			break;
		if (job->status != STS_COMPLETED) {
			pr_err("%s: hmac md5: job %d not completed\n",
				args->name, j);
		}
	}
	if (j != jobs)
		pr_err("%s: hmac md5: only processed %d of %d jobs\n",
			args->name, j, jobs);
}

static void stress_hmac_sha1(
	const args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs)
{
	int j;
	size_t i;
	struct JOB_AES_HMAC *job;

	const int digest_size = 20;
	const int block_size = 64;
	uint8_t key[block_size] ALIGNED(16);
	uint8_t buf[block_size] ALIGNED(16);
	uint8_t ipad_hash[digest_size] ALIGNED(16);
	uint8_t opad_hash[digest_size] ALIGNED(16);
	uint8_t output[jobs * digest_size] ALIGNED(16);

	stress_rnd_fill(key, sizeof(key));
	for (i = 0; i < sizeof(key); i++)
		buf[i] = key[i] ^ 0x36;
	IMB_MD5_ONE_BLOCK(mb_mgr, buf, ipad_hash);
	for (i = 0; i < sizeof(key); i++)
		buf[i] = key[i] ^ 0x5c;
	IMB_MD5_ONE_BLOCK(mb_mgr, buf, opad_hash);

	stress_job_empty(mb_mgr);

	for (j = 0; j < jobs; j++) {
		uint8_t *dst = output + (j * digest_size);

		job = IMB_GET_NEXT_JOB(mb_mgr);
		(void)memset(job, 0, sizeof(*job));

		job->aes_enc_key_expanded = NULL;
		job->aes_dec_key_expanded = NULL;
		job->cipher_direction = ENCRYPT;
		job->chain_order = HASH_CIPHER;
		job->dst = NULL;
		job->aes_key_len_in_bytes = 0;
		job->auth_tag_output = dst;
		job->auth_tag_output_len_in_bytes = digest_size;
		job->iv = NULL;
		job->iv_len_in_bytes = 0;
		job->src = data;
		job->cipher_start_src_offset_in_bytes = 0;
		job->msg_len_to_cipher_in_bytes = 0;
		job->hash_start_src_offset_in_bytes = 0;
		job->msg_len_to_hash_in_bytes = data_len;
		job->u.HMAC._hashed_auth_key_xor_ipad = ipad_hash;
		job->u.HMAC._hashed_auth_key_xor_opad = opad_hash;
		job->cipher_mode = NULL_CIPHER;
		job->hash_alg = SHA1;
		job->user_data = dst;
		job = IMB_SUBMIT_JOB(mb_mgr);
		(void)job;
	}

	for (j = 0; j < jobs; j++) {
		job = IMB_FLUSH_JOB(mb_mgr);
		if (!job)
			break;
		if (job->status != STS_COMPLETED) {
			pr_err("%s: hmac sha1: job %d not completed\n",
				args->name, j);
		}
	}
	if (j != jobs)
		pr_err("%s: hmac sha1: only processed %d of %d jobs\n",
			args->name, j, jobs);
}

static void stress_hmac_sha512(
	const args_t *args,
	struct MB_MGR *mb_mgr,
	const uint8_t *data,
	const size_t data_len,
	const int jobs)
{
	int j;
	size_t i;
	struct JOB_AES_HMAC *job;

	const int digest_size = SHA512_DIGEST_SIZE_IN_BYTES;
	const int block_size = SHA_512_BLOCK_SIZE;
	uint8_t rndkey[block_size] ALIGNED(16);
	uint8_t key[block_size] ALIGNED(16);
	uint8_t buf[block_size] ALIGNED(16);
	uint8_t ipad_hash[digest_size] ALIGNED(16);
	uint8_t opad_hash[digest_size] ALIGNED(16);
	uint8_t output[jobs * digest_size] ALIGNED(16);

	stress_rnd_fill(rndkey, sizeof(rndkey));

	IMB_SHA512(mb_mgr, rndkey, SHA_512_BLOCK_SIZE, key);

	for (i = 0; i < sizeof(key); i++)
		buf[i] = key[i] ^ 0x36;
	IMB_SHA512_ONE_BLOCK(mb_mgr, buf, ipad_hash);
	for (i = 0; i < sizeof(key); i++)
		buf[i] = key[i] ^ 0x5c;
	IMB_SHA512_ONE_BLOCK(mb_mgr, buf, opad_hash);

	stress_job_empty(mb_mgr);

	for (j = 0; j < jobs; j++) {
		uint8_t *dst = output + (j * digest_size);

		job = IMB_GET_NEXT_JOB(mb_mgr);
		(void)memset(job, 0, sizeof(*job));

		job->aes_enc_key_expanded = NULL;
		job->aes_dec_key_expanded = NULL;
		job->cipher_direction = ENCRYPT;
		job->chain_order = HASH_CIPHER;
		job->dst = NULL;
		job->aes_key_len_in_bytes = 0;
		job->auth_tag_output = dst;
		job->auth_tag_output_len_in_bytes = digest_size;
		job->iv = NULL;
		job->iv_len_in_bytes = 0;
		job->src = data;
		job->cipher_start_src_offset_in_bytes = 0;
		job->msg_len_to_cipher_in_bytes = 0;
		job->hash_start_src_offset_in_bytes = 0;
		job->msg_len_to_hash_in_bytes = data_len;
		job->u.HMAC._hashed_auth_key_xor_ipad = ipad_hash;
		job->u.HMAC._hashed_auth_key_xor_opad = opad_hash;
		job->cipher_mode = NULL_CIPHER;
		job->hash_alg = SHA_512;
		job->user_data = dst;
		job = IMB_SUBMIT_JOB(mb_mgr);
		(void)job;
	}

	for (j = 0; j < jobs; j++) {
		job = IMB_FLUSH_JOB(mb_mgr);
		if (!job)
			break;
		if (job->status != STS_COMPLETED) {
			pr_err("%s: hmac sha512: job %d not completed\n",
				args->name, j);
		}
	}
	if (j != jobs)
		pr_err("%s: hmac sha512: only processed %d of %d jobs\n",
			args->name, j, jobs);
}


typedef struct {
	const int features;
	const char *name;
	void (*init_func)(MB_MGR *p_mgr);
} init_mb_t;

static init_mb_t init_mb[] = {
	{ FEATURE_SSE,		"sse",		init_mb_mgr_sse },
	{ FEATURE_AVX,		"avx",		init_mb_mgr_avx },
	{ FEATURE_AVX2,		"avx2",		init_mb_mgr_avx2 },
	{ FEATURE_AVX512,	"avx512",	init_mb_mgr_avx512 },
};

/*
 *  stress_ipsec_mb()
 *      stress Intel ipsec_mb instruction
 */
static int stress_ipsec_mb(const args_t *args)
{
	MB_MGR *p_mgr = NULL;
	int features;
	uint8_t data[8192] ALIGNED(64);
	const size_t n_features = SIZEOF_ARRAY(init_mb);
	double t[n_features];
	size_t i;
	uint64_t count = 0;
	bool got_features = false;

	p_mgr = alloc_mb_mgr(0);
	if (!p_mgr) {
		pr_inf("%s: failed to setup Intel IPSEC MB library, skipping\n", args->name);
		return EXIT_NO_RESOURCE;
	}
	if (imb_get_version() < IMB_VERSION(0, 51, 0)) {
		pr_inf("%s: version %s of Intel IPSEC MB library is too low, skipping\n", 
			args->name, imb_get_version_str());
		free_mb_mgr(p_mgr);
		return EXIT_NOT_IMPLEMENTED;
	}

	features = stress_ipsec_mb_features(args, p_mgr);

	for (i = 0; i < n_features; i++) {
		t[i] = 0.0;
		if ((init_mb[i].features & features) == init_mb[i].features) {
			got_features = true;
			break;
		}
	}
	if (!got_features) {
		pr_inf("%s: not enough CPU features to support Intel IPSEC MB library, skipping\n", args->name);
		free_mb_mgr(p_mgr);
		return EXIT_NOT_IMPLEMENTED;
	}

	stress_rnd_fill(data, sizeof(data));

	do {
		for (i = 0; i < n_features; i++) {
			if ((init_mb[i].features & features) == init_mb[i].features) {
				double t1, t2;

				t1 = time_now();
				init_mb[i].init_func(p_mgr);
				stress_cmac(args, p_mgr, data, sizeof(data), 1);
				stress_ctr(args, p_mgr, data, sizeof(data), 1);
				stress_des(args, p_mgr, data, sizeof(data), 1);
				stress_hmac_md5(args, p_mgr, data, sizeof(data), 1);
				stress_hmac_sha1(args, p_mgr, data, sizeof(data), 1);
				stress_hmac_sha512(args, p_mgr, data, sizeof(data), 1);
				stress_sha(args, p_mgr, data, sizeof(data), 1);
				t2 = time_now();
				t[i] += (t2 - t1);
			}
		}
		count++;
		inc_counter(args);
	} while (keep_stressing());

	for (i = 0; i < n_features; i++) {
		if (((init_mb[i].features & features) == init_mb[i].features) && (t[i] > 0.0))
			pr_dbg("%s: %s %.3f bogo/ops per second\n",
				args->name, init_mb[i].name,
				(float)count / t[i]);
	}

	free_mb_mgr(p_mgr);

	return EXIT_SUCCESS;
}

stressor_info_t stress_ipsec_mb_info = {
	.stressor = stress_ipsec_mb,
	.supported = stress_ipsec_mb_supported,
	.class = CLASS_CPU,
	.help = help
};
#else

static int stress_ipsec_mb_supported(void)
{
	pr_inf("ipsec_mb stressor will be skipped, CPU "
		"needs to be an x86-64 and a recent IPSEC MB library is required.\n");
	return -1;
}

stressor_info_t stress_ipsec_mb_info = {
	.stressor = stress_not_implemented,
	.supported = stress_ipsec_mb_supported,
	.class = CLASS_CPU,
	.help = help
};
#endif
