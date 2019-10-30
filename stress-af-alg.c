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

/* See https://lwn.net/Articles/410833/ */

typedef enum {
	CRYPTO_AHASH,
	CRYPTO_SHASH,
	CRYPTO_CIPHER,
	CRYPTO_AKCIPHER,
	CRYPTO_SKCIPHER,
	CRYPTO_RNG,
	CRYPTO_UNKNOWN,
} crypto_type_t;

typedef struct crypto_info {
	crypto_type_t	crypto_type;
	char 	*type;
	char 	*name;
	int 	block_size;
	int	max_key_size;
	int	iv_size;
	int	digest_size;
	struct crypto_info *next;
} crypto_info_t;

static crypto_info_t *crypto_info_list;

static int stress_af_alg_hash(
	const args_t *args,
	const int sockfd,
	const crypto_info_t *info)
{
	int fd;
	ssize_t j;
	const ssize_t digest_size = info->digest_size;
	char input[DATA_LEN];
	char digest[digest_size];
	struct sockaddr_alg sa;

	(void)memset(&sa, 0, sizeof(sa));
	sa.salg_family = AF_ALG;
	(void)shim_strlcpy((char *)sa.salg_type, info->type, sizeof(sa.salg_type));
	(void)shim_strlcpy((char *)sa.salg_name, info->name, sizeof(sa.salg_name) - 1);

	if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	/* Perhaps the hash does not exist with this kernel */
		if (errno == ENOENT)
			return EXIT_SUCCESS;
		if (errno == EBUSY)
			return EXIT_SUCCESS;
		pr_fail_err("bind");
		return EXIT_FAILURE;
	}

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
			if ((errno == 0) || (errno == ENOKEY) || (errno == ENOENT))
				return EXIT_SUCCESS;
			pr_fail("%s: send using %s failed: errno=%d (%s)\n",
					args->name, info->name,
					errno, strerror(errno));
			(void)close(fd);
			return EXIT_FAILURE;
		}
		if (recv(fd, digest, digest_size, MSG_WAITALL) != digest_size) {
			pr_fail("%s: recv using %s failed: errno=%d (%s)\n",
				args->name, info->name,
				errno, strerror(errno));
			(void)close(fd);
			return EXIT_FAILURE;
		}
		inc_counter(args);
		if (args->max_ops && (get_counter(args) >= args->max_ops)) {
			(void)close(fd);
			return EXIT_SUCCESS;
		}
		(void)close(fd);
	}

	return EXIT_SUCCESS;
}

static int stress_af_alg_cipher(
	const args_t *args,
	const int sockfd,
	const crypto_info_t *info)
{
	int fd;
	ssize_t j;
	struct sockaddr_alg sa;
	const ssize_t key_size = info->max_key_size;
	const ssize_t iv_size = info->iv_size;
	char key[key_size];
	char input[DATA_LEN], output[DATA_LEN];

	(void)memset(&sa, 0, sizeof(sa));
	sa.salg_family = AF_ALG;
	(void)shim_strlcpy((char *)sa.salg_type, info->type, sizeof(sa.salg_type));
	(void)shim_strlcpy((char *)sa.salg_name, info->name, sizeof(sa.salg_name) - 1);

	if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		/* Perhaps the cipher does not exist with this kernel */
		if ((errno == 0) || (errno == ENOKEY) || (errno == ENOENT) || (errno == EBUSY))
			return EXIT_SUCCESS;
		pr_fail_err("bind");
		return EXIT_FAILURE;
	}

	stress_strnrnd(key, sizeof(key));
	if (setsockopt(sockfd, SOL_ALG, ALG_SET_KEY, key, sizeof(key)) < 0) {
		pr_fail_err("setsockopt");
		return EXIT_FAILURE;
	}

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
				args->name, info->name,
				errno, strerror(errno));
			(void)close(fd);
			return EXIT_FAILURE;
		}
		if (read(fd, output, sizeof(output)) != sizeof(output)) {
			pr_fail("%s: read using %s failed: errno=%d (%s)\n",
				args->name, info->name,
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
				args->name, info->name,
				errno, strerror(errno));
			(void)close(fd);
			return EXIT_FAILURE;
		}
		if (read(fd, output, sizeof(output)) != sizeof(output)) {
			pr_fail("%s: read using %s failed: errno=%d (%s)\n",
				args->name, info->name,
				errno, strerror(errno));
			(void)close(fd);
			return EXIT_FAILURE;
		} else {
			if (memcmp(input, output, sizeof(input))) {
				pr_err("%s: decrypted data "
					"different from original data "
					"using %s\n",
					args->name, info->name);
			}
		}
	}

	(void)close(fd);
	inc_counter(args);

	return EXIT_SUCCESS;
}

static int stress_af_alg_rng(
	const args_t *args,
	const int sockfd,
	const crypto_info_t *info)
{
	int fd;
	ssize_t j;
	struct sockaddr_alg sa;

	(void)memset(&sa, 0, sizeof(sa));
	sa.salg_family = AF_ALG;
	(void)shim_strlcpy((char *)sa.salg_type, "rng", sizeof(sa.salg_type));
	(void)shim_strlcpy((char *)sa.salg_name, info->name, sizeof(sa.salg_name) - 1);

	if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		/* Perhaps the rng does not exist with this kernel */
		if ((errno == ENOENT) || (errno == EBUSY))
			return EXIT_SUCCESS;
		pr_fail_err("bind");
		return EXIT_FAILURE;
	}

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
			if (errno != EINVAL) {
				pr_fail_err("read");
				(void)close(fd);
				return EXIT_FAILURE;
			}
		}
		inc_counter(args);
		if (args->max_ops && (get_counter(args) >= args->max_ops)) {
			(void)close(fd);
			return EXIT_SUCCESS;
		}
	}
	(void)close(fd);

	return EXIT_SUCCESS;
}

static int stress_af_alg_count_crypto(void)
{
	int count = 0;
	crypto_info_t *ci;

	/* Scan for duplications */
	for (ci = crypto_info_list; ci; ci = ci->next)
		count++;

	return count;
}

/*
 *  stress_af_alg()
 *	stress socket AF_ALG domain
 */
static int stress_af_alg(const args_t *args)
{
	int sockfd = -1, rc = EXIT_FAILURE;
	int retries = MAX_AF_ALG_RETRIES;
	const int count = stress_af_alg_count_crypto();

	if (count == 0) {
		pr_inf("%s: no cryptographic algorithms found in /proc/crypto",
			args->name);
	}

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

	if (args->instance == 0) {
		pr_inf("%s: exercising %d cryptographic algorithms\n",
			args->name, count);
	}

	do {
		crypto_info_t *info;

		for (info = crypto_info_list; info; info = info->next) {
			switch (info->crypto_type) {
			case CRYPTO_AHASH:
			case CRYPTO_SHASH:
				rc = stress_af_alg_hash(args, sockfd, info);
				(void)rc;
				break;
			case CRYPTO_CIPHER:
			case CRYPTO_AKCIPHER:
			case CRYPTO_SKCIPHER:
				rc = stress_af_alg_cipher(args, sockfd, info);
				(void)rc;
				break;
			case CRYPTO_RNG:
				rc = stress_af_alg_rng(args, sockfd, info);
				(void)rc;
				break;
			default:
				break;
			}
		}
	} while (keep_stressing());

	rc = EXIT_SUCCESS;
	(void)close(sockfd);

	return rc;
}

/*
 *  dup_field()
 * 	duplicate a text string field
 */
static char *dup_field(const char *buffer)
{
	char *ptr = strchr(buffer, ':');
	char *eol = strchr(buffer, '\n');

	if (!ptr)
		return NULL;
	if (eol)
		*eol = '\0';

	return strdup(ptr + 2);
}

/*
 *   type_field()
 *	map text type name to symbolic enum value
 */
static crypto_type_t type_field(const char *buffer)
{
	char *ptr = strchr(buffer, ':');

	if (!ptr)
		return CRYPTO_UNKNOWN;
	if (!strncmp("cipher", ptr + 2, 6))
		return CRYPTO_CIPHER;
	if (!strncmp("akcipher", ptr + 2, 8))
		return CRYPTO_AKCIPHER;
	if (!strncmp("skcipher", ptr + 2, 8))
		return CRYPTO_SKCIPHER;
	if (!strncmp("ahash", ptr + 2, 5))
		return CRYPTO_AHASH;
	if (!strncmp("shash", ptr + 2, 5))
		return CRYPTO_SHASH;
	if (!strncmp("rng", ptr + 2, 3))
		return CRYPTO_RNG;
	return CRYPTO_UNKNOWN;
}

/*
 *  int_field()
 *	parse an integer from a numeric field
 */
static int int_field(const char *buffer)
{
	char *ptr = strchr(buffer, ':');

	if (!ptr)
		return -1;
	return atoi(ptr + 2);
}

/*
 *  stress_af_alg_add_crypto()
 *	add crypto algorithm to list if it is unique
 */
static void stress_af_alg_add_crypto(crypto_info_t *info)
{
	crypto_info_t *ci;

	/* Don't add info with empty text fields */
	if ((info->name == NULL) || (info->type == NULL))
		return;

	/* Scan for duplications */
	for (ci = crypto_info_list; ci; ci = ci->next) {
		if (strcmp(ci->name, info->name) == 0 &&
		    strcmp(ci->type, info->type) == 0 &&
		    ci->block_size == info->block_size &&
		    ci->max_key_size == info->max_key_size &&
		    ci->iv_size == info->iv_size &&
		    ci->digest_size == info->digest_size)
			return;
	}
	/*
	 *  Add new item, if we can't allocate, silently
	 *  ignore failure and don't add.
	 */
	ci = malloc(sizeof(*ci));
	if (!ci)
		return;
	*ci = *info;
	ci->next = crypto_info_list;
	crypto_info_list = ci;
}

/*
 *  stress_af_alg_init()
 *	populate cryto info list from data from /proc/crypto
 */
static void stress_af_alg_init(void)
{
	FILE *fp;
	char buffer[1024];
	crypto_info_t info;

	crypto_info_list = NULL;
	fp = fopen("/proc/crypto", "r");
	if (!fp)
		return;

	(void)memset(buffer, 0, sizeof(buffer));
	(void)memset(&info, 0, sizeof(info));

	while (fgets(buffer, sizeof(buffer) - 1, fp)) {
		if (!strncmp(buffer, "name", 4))
			info.name = dup_field(buffer);
		else if (!strncmp(buffer, "type", 4)) {
			info.crypto_type = type_field(buffer);
			info.type = dup_field(buffer);
		}
		else if (!strncmp(buffer, "blocksize", 9))
			info.block_size = int_field(buffer);
		else if (!strncmp(buffer, "max keysize", 11))
			info.max_key_size = int_field(buffer);
		else if (!strncmp(buffer, "ivsize", 6))
			info.iv_size = int_field(buffer);
		else if (!strncmp(buffer, "digestsize", 10))
			info.digest_size = int_field(buffer);
		else if (buffer[0] == '\n') {
			if (info.crypto_type != CRYPTO_UNKNOWN)
				stress_af_alg_add_crypto(&info);
			(void)memset(&info, 0, sizeof(info));
		}
	}

	(void)fclose(fp);
}

/*
 *  stress_af_alg_deinit()
 *	free crypto info list
 */
static void stress_af_alg_deinit(void)
{
	crypto_info_t *ci = crypto_info_list;

	while (ci) {
		crypto_info_t *next = ci->next;

		free(ci->name);
		free(ci->type);
		free(ci);
		ci = next;
	}
	crypto_info_list = NULL;
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
