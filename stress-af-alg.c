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
	{ NULL,	"af-alg N",	"start N workers that stress AF_ALG socket domain" },
	{ NULL,	"af-alg-ops N",	"stop after N af-alg bogo operations" },
	{ NULL, NULL,		NULL }
};

#if defined(HAVE_LINUX_IF_ALG_H) && \
    defined(HAVE_LINUX_SOCKET_H) && \
    defined(AF_ALG)

#if !defined(SOL_ALG)
#define SOL_ALG				(279)
#endif
#define DATA_LEN 			(1024)
#define MAX_AF_ALG_RETRIES		(25)

#define SNG_CRC32C_DIGEST_SIZE		(4)
#define SNG_SHA1_DIGEST_SIZE		(20)
#define SNG_SHA224_DIGEST_SIZE		(28)
#define SNG_SHA256_DIGEST_SIZE		(32)
#define SNG_SHA384_DIGEST_SIZE		(48)
#define SNG_SHA512_DIGEST_SIZE		(64)
#define SNG_MD4_DIGEST_SIZE		(16)
#define SNG_MD5_DIGEST_SIZE		(16)
#define SNG_RMD128_DIGEST_SIZE		(16)
#define SNG_RMD160_DIGEST_SIZE		(20)
#define SNG_RMD256_DIGEST_SIZE		(32)
#define SNG_RMD320_DIGEST_SIZE		(40)
#define SNG_WP256_DIGEST_SIZE		(32)
#define SNG_WP384_DIGEST_SIZE		(48)
#define SNG_WP512_DIGEST_SIZE		(64)
#define SNG_TGR128_DIGEST_SIZE		(16)
#define SNG_TGR160_DIGEST_SIZE		(20)
#define SNG_TGR192_DIGEST_SIZE		(24)
#define SNG_XOR_DIGEST_SIZE		(4096)
#define SNG_CRC_T10DIF_DIGEST_SIZE	(2)

#define SNG_CRC32C_BLOCK_SIZE		(1)
#define SNG_AES_BLOCK_SIZE		(16)
#define SNG_TF_BLOCK_SIZE		(16)
#define SNG_SERPENT_BLOCK_SIZE		(16)
#define SNG_CAST6_BLOCK_SIZE		(16)
#define SNG_CAMELLIA_BLOCK_SIZE		(16)
#define SNG_SALSA20_BLOCK_SIZE		(8)
#define SNG_GHASH_BLOCK_SIZE		(16)
#define SNG_TWOFISH_BLOCK_SIZE		(16)

#define SNG_AES_MAX_KEY_SIZE		(32)
#define SNG_TF_MAX_KEY_SIZE		(32)
#define SNG_SERPENT_MAX_KEY_SIZE	(32)
#define SNG_CAST6_MAX_KEY_SIZE		(32)
#define SNG_CAMELLIA_MAX_KEY_SIZE	(32)
#define SNG_SALSA20_MAX_KEY_SIZE	(32)
#define SNG_GHASH_MAX_KEY_SIZE		(16)
#define SNG_TWOFISH_MAX_KEY_SIZE	(32)

/* See https://lwn.net/Articles/410833/ */

typedef struct {
	const char *name;
	const ssize_t digest_size;
} alg_hash_info_t;

typedef struct {
	const char *name;
	const ssize_t block_size;
	const ssize_t key_size;
} alg_cipher_info_t;

typedef struct {
	const char *name;
} alg_rng_info_t;

static size_t af_alg_hash_skip_sz;
static size_t af_alg_cipher_skip_sz;

static alg_hash_info_t algo_hash_info[] = {
	{ "crc32c",	SNG_CRC32C_DIGEST_SIZE },
	{ "sha1",	SNG_SHA1_DIGEST_SIZE },
	{ "sha224",	SNG_SHA224_DIGEST_SIZE },
	{ "sha256",	SNG_SHA256_DIGEST_SIZE },
	{ "sha384",	SNG_SHA384_DIGEST_SIZE },
	{ "sha512",	SNG_SHA512_DIGEST_SIZE },
	{ "md4",	SNG_MD4_DIGEST_SIZE },
	{ "md5",	SNG_MD5_DIGEST_SIZE },
	{ "rmd128",	SNG_RMD128_DIGEST_SIZE },
	{ "rmd160",	SNG_RMD160_DIGEST_SIZE },
	{ "rmd256",	SNG_RMD256_DIGEST_SIZE },
	{ "rmd320",	SNG_RMD320_DIGEST_SIZE },
	{ "wp256",	SNG_WP256_DIGEST_SIZE },
	{ "wp384",	SNG_WP384_DIGEST_SIZE },
	{ "wp512",	SNG_WP512_DIGEST_SIZE },
	{ "tgr128",	SNG_TGR128_DIGEST_SIZE },
	{ "tgr160",	SNG_TGR160_DIGEST_SIZE },
	{ "tgr192",	SNG_TGR192_DIGEST_SIZE },
	{ "xor",	SNG_XOR_DIGEST_SIZE },
	{ "crct10dif",	SNG_CRC_T10DIF_DIGEST_SIZE },
};

static alg_cipher_info_t algo_cipher_info[] = {
	{ "cbc(aes)",		SNG_AES_BLOCK_SIZE,	SNG_AES_MAX_KEY_SIZE },
	{ "lrw(aes)",		SNG_AES_BLOCK_SIZE,	SNG_AES_MAX_KEY_SIZE },
	{ "ofb(aes)",		SNG_AES_BLOCK_SIZE,	SNG_AES_MAX_KEY_SIZE },
	{ "xts(twofish)",	SNG_TF_BLOCK_SIZE,	SNG_TF_MAX_KEY_SIZE },
	{ "xts(serpent)",	SNG_SERPENT_BLOCK_SIZE,	SNG_SERPENT_MAX_KEY_SIZE },
	{ "xts(cast6)",		SNG_CAST6_BLOCK_SIZE,	SNG_CAST6_MAX_KEY_SIZE },
	{ "xts(camellia)",	SNG_CAMELLIA_BLOCK_SIZE,SNG_CAMELLIA_MAX_KEY_SIZE },
	{ "lrw(twofish)",	SNG_TF_BLOCK_SIZE,	SNG_TF_MAX_KEY_SIZE },
	{ "lrw(serpent)",	SNG_SERPENT_BLOCK_SIZE,	SNG_SERPENT_MAX_KEY_SIZE },
	{ "lrw(cast6)",		SNG_CAST6_BLOCK_SIZE,	SNG_CAST6_MAX_KEY_SIZE },
	{ "lrw(camellia)",	SNG_CAMELLIA_BLOCK_SIZE,SNG_CAMELLIA_MAX_KEY_SIZE },
	{ "salsa20",		SNG_SALSA20_BLOCK_SIZE,	SNG_SALSA20_MAX_KEY_SIZE },
	{ "ghash",		SNG_GHASH_BLOCK_SIZE,	SNG_GHASH_MAX_KEY_SIZE },
	{ "twofish",		SNG_TWOFISH_BLOCK_SIZE,	SNG_TWOFISH_MAX_KEY_SIZE },
};

static const alg_rng_info_t algo_rng_info[] = {
	{ "jitterentropy_rng" }
};

static int stress_af_alg_hash(
	const args_t *args,
	const int sockfd,
	uint64_t *hashfails)
{
	size_t i;
	bool bind_ok = false;

	for (i = 0; i < SIZEOF_ARRAY(algo_hash_info); i++) {
		int fd;
		ssize_t j;
		const ssize_t digest_size = algo_hash_info[i].digest_size;
		char input[DATA_LEN], digest[digest_size];
		struct sockaddr_alg sa;

		if (!keep_stressing())
			break;
		if (g_shared->af_alg_hash_skip[i])
			continue;

		(void)memset(&sa, 0, sizeof(sa));
		sa.salg_family = AF_ALG;
		(void)shim_strlcpy((char *)sa.salg_type, "hash", sizeof(sa.salg_type));
		(void)shim_strlcpy((char *)sa.salg_name, algo_hash_info[i].name,
			sizeof(sa.salg_name) - 1);

		if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
			/* Perhaps the hash does not exist with this kernel */
			if (errno == ENOENT) {
				g_shared->af_alg_hash_skip[i] = true;
				continue;
			}
			if (errno == EBUSY)
				continue;
			pr_fail_err("bind");
			return EXIT_FAILURE;
		}
		bind_ok = true;

		if (!keep_stressing())
			break;

		fd = accept(sockfd, NULL, 0);
		if (fd < 0) {
			pr_fail_err("accept");
			return EXIT_FAILURE;
		}

		stress_strnrnd(input, sizeof(input));

		for (j = 32; j < (ssize_t)sizeof(input); j += 32) {
			if (!keep_stressing())
				break;
			if (send(fd, input, j, 0) != j) {
				if ((errno == 0) || (errno == ENOKEY) || (errno == ENOENT)) {
					g_shared->af_alg_hash_skip[i] = true;
					continue;
				}
				pr_fail("%s: send using %s failed: errno=%d (%s)\n",
					args->name, algo_hash_info[i].name,
					errno, strerror(errno));
				(void)close(fd);
				return EXIT_FAILURE;
			}
			if (recv(fd, digest, digest_size, MSG_WAITALL) != digest_size) {
				pr_fail("%s: recv using %s failed: errno=%d (%s)\n",
					args->name, algo_hash_info[i].name,
					errno, strerror(errno));
				(void)close(fd);
				return EXIT_FAILURE;
			}
			inc_counter(args);
			if (args->max_ops && (get_counter(args) >= args->max_ops)) {
				(void)close(fd);
				return EXIT_SUCCESS;
			}
		}
		(void)close(fd);
	}
	if (!bind_ok)
		(*hashfails)++;

	return EXIT_SUCCESS;
}

static int stress_af_alg_cipher(
	const args_t *args,
	const int sockfd,
	uint64_t *cipherfails)
{
	size_t i;
	bool bind_ok = false;

	for (i = 0; i < SIZEOF_ARRAY(algo_cipher_info); i++) {
		int fd;
		ssize_t j;
		struct sockaddr_alg sa;
		const ssize_t key_size = algo_cipher_info[i].key_size;
		const ssize_t block_size = algo_cipher_info[i].block_size;
		const ssize_t iv_size = block_size;
		char key[key_size];
		char input[DATA_LEN], output[DATA_LEN];

		if (!keep_stressing())
			break;
		if (g_shared->af_alg_cipher_skip[i])
			continue;

		(void)memset(&sa, 0, sizeof(sa));
		sa.salg_family = AF_ALG;
		(void)shim_strlcpy((char *)sa.salg_type, "skcipher", sizeof(sa.salg_type));
		(void)shim_strlcpy((char *)sa.salg_name, algo_cipher_info[i].name,
			sizeof(sa.salg_name) - 1);

		if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
			/* Perhaps the cipher does not exist with this kernel */
			if ((errno == 0) || (errno == ENOKEY) || (errno == ENOENT) || (errno == EBUSY)) {
				g_shared->af_alg_cipher_skip[i] = true;
				continue;
			}
			pr_fail_err("bind");
			return EXIT_FAILURE;
		}
		bind_ok = true;

		stress_strnrnd(key, sizeof(key));
		if (setsockopt(sockfd, SOL_ALG, ALG_SET_KEY, key, sizeof(key)) < 0) {
			pr_fail_err("setsockopt");
			return EXIT_FAILURE;
		}

		if (!keep_stressing())
			break;

		fd = accept(sockfd, NULL, 0);
		if (fd < 0) {
			pr_fail_err("accept");
			return EXIT_FAILURE;
		}

		for (j = 32; j < (ssize_t)sizeof(input); j += 32) {
			__u32 *u32ptr;
			struct msghdr msg;
			struct cmsghdr *cmsg;
			char cbuf[CMSG_SPACE(sizeof(__u32)) +
				CMSG_SPACE(4) + CMSG_SPACE(iv_size)];
			struct af_alg_iv *iv;	/* Initialisation Vector */
			struct iovec iov;

			if (!keep_stressing())
				break;
			(void)memset(&msg, 0, sizeof(msg));
			(void)memset(cbuf, 0, sizeof(cbuf));

			msg.msg_control = cbuf;
			msg.msg_controllen = sizeof(cbuf);

			/* Chosen operation - ENCRYPT */
			cmsg = CMSG_FIRSTHDR(&msg);
			/* Keep static analysis happy */
			if (!cmsg) {
				(void)close(fd);
				pr_fail_err("null cmsg");
				return EXIT_FAILURE;
			}
			cmsg->cmsg_level = SOL_ALG;
			cmsg->cmsg_type = ALG_SET_OP;
			cmsg->cmsg_len = CMSG_LEN(4);
			u32ptr = (__u32 *)CMSG_DATA(cmsg);
			*u32ptr = ALG_OP_ENCRYPT;

			/* Set up random Initialization Vector */
			cmsg = CMSG_NXTHDR(&msg, cmsg);
			if (!cmsg)
				break;
			cmsg->cmsg_level = SOL_ALG;
			cmsg->cmsg_type = ALG_SET_IV;
			cmsg->cmsg_len = CMSG_LEN(4) + CMSG_LEN(iv_size);
			iv = (void *)CMSG_DATA(cmsg);
			iv->ivlen = iv_size;

			stress_strnrnd((char *)iv->iv, iv_size);

			/* Generate random message to encrypt */
			stress_strnrnd(input, sizeof(input));
			iov.iov_base = input;
			iov.iov_len = sizeof(input);

			msg.msg_iov = &iov;
			msg.msg_iovlen = 1;

			if (sendmsg(fd, &msg, 0) < 0) {
				if (errno == ENOMEM)
					break;
				pr_fail("%s: sendmsg using %s failed: errno=%d (%s)\n",
					args->name, algo_cipher_info[i].name,
					errno, strerror(errno));
				(void)close(fd);
				return EXIT_FAILURE;
			}
			if (read(fd, output, sizeof(output)) != sizeof(output)) {
				pr_fail("%s: read using %s failed: errno=%d (%s)\n",
					args->name, algo_cipher_info[i].name,
					errno, strerror(errno));
				(void)close(fd);
				return EXIT_FAILURE;
			}

			/* Chosen operation - DECRYPT */
			cmsg = CMSG_FIRSTHDR(&msg);
			if (!cmsg)
				break;
			cmsg->cmsg_level = SOL_ALG;
			cmsg->cmsg_type = ALG_SET_OP;
			cmsg->cmsg_len = CMSG_LEN(4);
			u32ptr = (__u32 *)CMSG_DATA(cmsg);
			*u32ptr = ALG_OP_DECRYPT;

			/* Set up random Initialization Vector */
			cmsg = CMSG_NXTHDR(&msg, cmsg);
			if (!cmsg)
				break;

			cmsg->cmsg_level = SOL_ALG;
			cmsg->cmsg_type = ALG_SET_IV;
			cmsg->cmsg_len = CMSG_LEN(4) + CMSG_LEN(iv_size);
			iv = (void *)CMSG_DATA(cmsg);
			iv->ivlen = iv_size;

			iov.iov_base = output;
			iov.iov_len = sizeof(output);

			msg.msg_iov = &iov;
			msg.msg_iovlen = 1;

			if (sendmsg(fd, &msg, 0) < 0) {
				if (errno == ENOMEM)
					break;
				pr_fail("%s: sendmsg using %s failed: errno=%d (%s)\n",
					args->name, algo_cipher_info[i].name,
					errno, strerror(errno));
				(void)close(fd);
				return EXIT_FAILURE;
			}
			if (read(fd, output, sizeof(output)) != sizeof(output)) {
				pr_fail("%s: read using %s failed: errno=%d (%s)\n",
					args->name, algo_cipher_info[i].name,
					errno, strerror(errno));
				(void)close(fd);
				return EXIT_FAILURE;
			} else {
				if (memcmp(input, output, sizeof(input))) {
					pr_err("%s: decrypted data "
						"different from original data "
						"using %s\n",
						args->name,  algo_hash_info[i].name);
				}
			}
		}

		(void)close(fd);
		inc_counter(args);
		if (args->max_ops && (get_counter(args) >= args->max_ops))
			return EXIT_SUCCESS;
	}
	if (!bind_ok)
		(*cipherfails)++;

	return EXIT_SUCCESS;
}

static int stress_af_alg_rng(
	const args_t *args,
	const int sockfd,
	uint64_t *rngfails)
{
	size_t i;
	bool bind_ok = false;

	for (i = 0; i < SIZEOF_ARRAY(algo_rng_info); i++) {
		int fd;
		ssize_t j;
		struct sockaddr_alg sa;

		if (!keep_stressing())
			break;
		(void)memset(&sa, 0, sizeof(sa));
		sa.salg_family = AF_ALG;
		(void)shim_strlcpy((char *)sa.salg_type, "rng", sizeof(sa.salg_type));
		(void)shim_strlcpy((char *)sa.salg_name, algo_rng_info[i].name,
			sizeof(sa.salg_name) - 1);

		if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
			/* Perhaps the rng does not exist with this kernel */
			if ((errno == ENOENT) || (errno == EBUSY))
				continue;
			pr_fail_err("bind");
			return EXIT_FAILURE;
		}
		bind_ok = true;

		if (!keep_stressing())
			break;

		fd = accept(sockfd, NULL, 0);
		if (fd < 0) {
			pr_fail_err("accept");
			return EXIT_FAILURE;
		}

		for (j = 0; j < 16; j++) {
			char output[16];

			if (!keep_stressing())
				break;
			if (read(fd, output, sizeof(output)) != sizeof(output)) {
				pr_fail_err("read");
				(void)close(fd);
				return EXIT_FAILURE;
			}
			inc_counter(args);
			if (args->max_ops && (get_counter(args) >= args->max_ops)) {
				(void)close(fd);
				return EXIT_SUCCESS;
			}
		}
		(void)close(fd);
	}
	if (!bind_ok)
		(*rngfails)++;

	return EXIT_SUCCESS;
}

/*
 *  stress_af_alg()
 *	stress socket AF_ALG domain
 */
static int stress_af_alg(const args_t *args)
{
	int sockfd = -1, rc = EXIT_FAILURE;
	int retries = MAX_AF_ALG_RETRIES;
	uint64_t hashfails = 0, cipherfails = 0, rngfails = 0;

	for (;;) {
		sockfd = socket(AF_ALG, SOCK_SEQPACKET, 0);
		if (sockfd >= 0)
			break;

		retries--;
		if ((!g_keep_stressing_flag) || (retries < 0) || (errno != EAFNOSUPPORT)) {
			if (errno == EAFNOSUPPORT) {
				/*
				 *  If we got got here, the protocol is not supported
				 *  so mark it as not implemented and skip the test
				 */
				return EXIT_NOT_IMPLEMENTED;
			}
			pr_fail_err("socket");
			return rc;
		}
		/*
		 * We may need to retry on EAFNOSUPPORT
		 * as udev may have to load in some
		 * cipher modules which can be racy or
		 * take some time
		 */
		(void)shim_usleep(200000);
	}

	do {
		rc = stress_af_alg_hash(args, sockfd, &hashfails);
		if (rc == EXIT_FAILURE)
			goto tidy;
		rc = stress_af_alg_cipher(args, sockfd, &cipherfails);
		if (rc == EXIT_FAILURE)
			goto tidy;
		rc = stress_af_alg_rng(args, sockfd, &rngfails);
		if (rc == EXIT_FAILURE)
			goto tidy;
	} while (keep_stressing());

	if (hashfails | cipherfails | rngfails)
		pr_dbg("%s: bind failed with ENOENT on all hashes (%"
			PRIu64 " times), ciphers (%" PRIu64 " times), "
			"prngs (%" PRIu64 " times)\n",
			args->name, hashfails, cipherfails, rngfails);
	rc = EXIT_SUCCESS;
tidy:
	(void)close(sockfd);

	return rc;
}

static bool *stress_af_alg_skip_alloc(size_t n, size_t *size)
{
	size_t page_size = stress_get_pagesize();
	size_t sz = (page_size + (n * sizeof(bool))) & ~(page_size - 1);
	bool *ptr;

	ptr = mmap(NULL, sz, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ptr == MAP_FAILED) {
		*size = 0;
		ptr = NULL;
	} else {
		*size = sz;
	}
	return ptr;
}

static void stress_af_alg_init(void)
{
	g_shared->af_alg_hash_skip = stress_af_alg_skip_alloc(
		SIZEOF_ARRAY(algo_hash_info), &af_alg_hash_skip_sz);
	g_shared->af_alg_cipher_skip = stress_af_alg_skip_alloc(
		SIZEOF_ARRAY(algo_cipher_info), &af_alg_cipher_skip_sz);
	
}

static void stress_af_alg_deinit(void)
{
	if (g_shared->af_alg_hash_skip && (af_alg_hash_skip_sz > 0)) {
		munmap(g_shared->af_alg_hash_skip, af_alg_hash_skip_sz);
		g_shared->af_alg_hash_skip = NULL;
		af_alg_hash_skip_sz = 0;
	}
	if (g_shared->af_alg_cipher_skip && (af_alg_cipher_skip_sz > 0)) {
		munmap(g_shared->af_alg_cipher_skip, af_alg_cipher_skip_sz);
		g_shared->af_alg_cipher_skip = NULL;
		af_alg_cipher_skip_sz = 0;
	}
}

stressor_info_t stress_af_alg_info = {
	.stressor = stress_af_alg,
	.init = stress_af_alg_init,
	.deinit = stress_af_alg_deinit,
	.class = CLASS_CPU | CLASS_OS,
	.help = help
};

#else
stressor_info_t stress_af_alg_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU | CLASS_OS,
	.help = help
};
#endif
