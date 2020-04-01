/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

#define ALLOC_SLOP	(64)

static const stress_help_t help[] = {
	{ NULL,	"af-alg N",	"start N workers that stress AF_ALG socket domain" },
	{ NULL,	"af-alg-ops N",	"stop after N af-alg bogo operations" },
	{ NULL,	"af-alg-dump",	"dump internal list from /proc/crypto to stdout" },
	{ NULL, NULL,		NULL }
};

static int stress_set_af_alg_dump(const char *opt)
{
	bool af_alg_dump = true;

	(void)opt;
	return stress_set_setting("af-alg-dump", TYPE_ID_BOOL, &af_alg_dump);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_af_alg_dump,	stress_set_af_alg_dump },
	{ 0,			NULL }
};

#if defined(HAVE_LINUX_IF_ALG_H) && \
    defined(HAVE_LINUX_SOCKET_H) && \
    defined(AF_ALG)

#if !defined(SOL_ALG)
#define SOL_ALG				(279)
#endif
#define DATA_LEN 			(1024)
#define MAX_AF_ALG_RETRIES		(25)
#define MAX_AF_ALG_RETRIES_BIND		(3)

/* See https://lwn.net/Articles/410833/ */

typedef enum {
	CRYPTO_AHASH,
	CRYPTO_SHASH,
	CRYPTO_CIPHER,
	CRYPTO_AKCIPHER,
	CRYPTO_SKCIPHER,
	CRYPTO_RNG,
	CRYPTO_AEAD,
	CRYPTO_UNKNOWN,
} stress_crypto_type_t;

typedef struct {
	const stress_crypto_type_t type;
	const char		*type_string;
	const char		*name;
} stress_crypto_type_info_t;

static const stress_crypto_type_info_t crypto_type_info[] = {
	{ CRYPTO_AHASH,		"CRYPTO_AHASH",		"ahash" },
	{ CRYPTO_SHASH,		"CRYPTO_SHASH",		"shash" },
	{ CRYPTO_CIPHER,	"CRYPTO_CIPHER",	"cipher" },
	{ CRYPTO_AKCIPHER,	"CRYPTO_AKCIPHER",	"akcipher" },
	{ CRYPTO_SKCIPHER,	"CRYPTO_SKCIPHER",	"skciper" },
	{ CRYPTO_RNG,		"CRYPTO_RNG",		"rng" },
	{ CRYPTO_AEAD,		"CRYPTO_AEAD",		"aead" },
	{ CRYPTO_UNKNOWN,	"CRYPTO_UNKNOWN",	"unknown" },
};

typedef struct stress_crypto_info {
	stress_crypto_type_t	crypto_type;
	char 	*type;
	char 	*name;
	int 	block_size;
	int	max_key_size;
	int	max_auth_size;
	int	iv_size;
	int	digest_size;
	bool	internal;
	bool	ignore;
	struct stress_crypto_info *next;
} stress_crypto_info_t;

static stress_crypto_info_t *crypto_info_list;

/*
 * Provide some predefined/default configs
 * to the list generated from /proc/crypto
 * so some not-loaded cryptographic module
 * (thus not yet present into /proc/crypto)
 * is loaded on-demand with bind() syscall.
 */
static stress_crypto_info_t crypto_info_defconfigs[] = {
#include "stress-af-alg-defconfigs.h"
};

static void stress_af_alg_add_crypto_defconfigs(void);

/*
 *   name_to_type()
 *	map text type name to symbolic enum value
 */
static stress_crypto_type_t name_to_type(const char *buffer)
{
	char *ptr = strchr(buffer, ':');
	size_t i;

	if (!ptr)
		return CRYPTO_UNKNOWN;
	ptr += 2;
	for (i = 0; i < SIZEOF_ARRAY(crypto_type_info); i++) {
		const size_t n = strlen(crypto_type_info[i].name);

		if (!strncmp(crypto_type_info[i].name, ptr, n))
			return crypto_type_info[i].type;
	}
	return CRYPTO_UNKNOWN;
}

/*
 *   type_to_name()
 *	map type to textual name
 */
static const char *type_to_name(const stress_crypto_type_t type)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(crypto_type_info); i++) {
		if (crypto_type_info[i].type == type)
			return crypto_type_info[i].name;
	}
	return "unknown";
}

/*
 *  stress_af_alg_ignore()
 *	some crypto engines may return EINVAL, so flag these up in
 *	debug and ignore them for the next iteration
 */
static void stress_af_alg_ignore(const stress_args_t *args, stress_crypto_info_t *info)
{
	if ((args->instance == 0) && (!info->ignore)) {
		pr_dbg("%s: sendmsg using %s failed with EINVAL, skipping crypto engine\n",
			args->name, info->name);
		info->ignore = true;
	}
}

static int stress_af_alg_hash(
	const stress_args_t *args,
	const int sockfd,
	stress_crypto_info_t *info)
{
	int fd, rc;
	ssize_t j;
	const ssize_t digest_size = info->digest_size;
	char *input, *digest;
	struct sockaddr_alg sa;
	int retries = MAX_AF_ALG_RETRIES_BIND;

	input = malloc(DATA_LEN + ALLOC_SLOP);
	if (!input)
		return EXIT_NO_RESOURCE;
	digest = malloc(digest_size + ALLOC_SLOP);
	if (!digest) {
		free(input);
		return EXIT_NO_RESOURCE;
	}

	(void)memset(&sa, 0, sizeof(sa));
	sa.salg_family = AF_ALG;
	(void)shim_strlcpy((char *)sa.salg_type, "hash", sizeof(sa.salg_type));
	(void)shim_strlcpy((char *)sa.salg_name, info->name, sizeof(sa.salg_name) - 1);

retry_bind:
	if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		/* Perhaps the hash does not exist with this kernel */
		if (errno == ENOENT) {
			rc = EXIT_SUCCESS;
			goto err;
		}
		if (errno == EBUSY) {
			rc = EXIT_SUCCESS;
			goto err;
		}
		if (errno == ETIMEDOUT && retries-- > 0)
			goto retry_bind;
		pr_fail_err("bind");
		rc = EXIT_FAILURE;
		goto err;
	}

	fd = accept(sockfd, NULL, 0);
	if (fd < 0) {
		pr_fail_err("accept");
		rc = EXIT_FAILURE;
		goto err;
	}

	stress_strnrnd(input, DATA_LEN);

	for (j = 32; j < DATA_LEN; j += 32) {
		if (!keep_stressing())
			break;
		if (send(fd, input, j, 0) != j) {
			if ((errno == 0) || (errno == ENOKEY) || (errno == ENOENT))
				continue;
			if (errno == EINVAL) {
				stress_af_alg_ignore(args, info);
				break;
			}
			pr_fail("%s: send using %s failed: errno=%d (%s)\n",
					args->name, info->name,
					errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto err_close;
		}
		if (recv(fd, digest, digest_size, MSG_WAITALL) != digest_size) {
			pr_fail("%s: recv using %s failed: errno=%d (%s)\n",
				args->name, info->name,
				errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto err_close;
		}
		inc_counter(args);
		if (args->max_ops && (get_counter(args) >= args->max_ops)) {
			rc = EXIT_SUCCESS;
			goto err_close;
		}
	}
	rc = EXIT_SUCCESS;
err_close:
	(void)close(fd);
err:
	free(digest);
	free(input);

	return rc;
}

static int stress_af_alg_cipher(
	const stress_args_t *args,
	const int sockfd,
	stress_crypto_info_t *info)
{
	int fd, rc;
	ssize_t j;
	struct sockaddr_alg sa;
	const ssize_t iv_size = info->iv_size;
	const ssize_t cbuf_size = CMSG_SPACE(sizeof(__u32)) +
				  CMSG_SPACE(4) + CMSG_SPACE(iv_size);
	char *input, *output, *cbuf;
	const char *salg_type = (info->crypto_type != CRYPTO_AEAD) ? "skcipher" : "aead";
	int retries = MAX_AF_ALG_RETRIES_BIND;

	input = malloc(DATA_LEN + ALLOC_SLOP);
	if (!input)
		return EXIT_NO_RESOURCE;
	output = malloc(DATA_LEN + ALLOC_SLOP);
	if (!output) {
		free(input);
		return EXIT_NO_RESOURCE;
	}
	cbuf = malloc(cbuf_size);
	if (!cbuf) {
		free(output);
		free(input);
		return EXIT_NO_RESOURCE;
	}

	(void)memset(&sa, 0, sizeof(sa));
	sa.salg_family = AF_ALG;
	(void)shim_strlcpy((char *)sa.salg_type, salg_type, sizeof(sa.salg_type));
	(void)shim_strlcpy((char *)sa.salg_name, info->name, sizeof(sa.salg_name) - 1);

retry_bind:
	if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		/* Perhaps the cipher does not exist with this kernel */
		if ((errno == 0) || (errno == ENOKEY) ||
                    (errno == ENOENT) || (errno == EBUSY)) {
			rc = EXIT_SUCCESS;
			goto err;
		}
		if (errno == ETIMEDOUT && retries-- > 0)
			goto retry_bind;
		pr_fail_err("bind");
		rc = EXIT_FAILURE;
		goto err;
	}

	if (info->crypto_type != CRYPTO_AEAD) {
#if defined(ALG_SET_KEY)
		char *key;

		key = calloc(1, info->max_key_size + ALLOC_SLOP);
		if (!key) {
			rc = EXIT_NO_RESOURCE;
			goto err;
		}

		stress_strnrnd(key, info->max_key_size);
		if (setsockopt(sockfd, SOL_ALG, ALG_SET_KEY, key, info->max_key_size) < 0) {
			free(key);
			if (errno == ENOPROTOOPT) {
				rc = EXIT_SUCCESS;
				goto err;
			}
			pr_fail_err("setsockopt");
			rc = EXIT_FAILURE;
			goto err;
		}
		free(key);
#else
		/* Not supported, skip */
		rc = EXIT_SUCCESS;
		goto err;
#endif
	} else  {
#if defined(ALG_SET_AEAD_ASSOCLEN)
		char *assocdata;

		assocdata = calloc(1, info->max_auth_size + ALLOC_SLOP);
		if (!assocdata) {
			rc = EXIT_NO_RESOURCE;
			goto err;
		}

		stress_strnrnd(assocdata, info->max_auth_size);
		if (setsockopt(sockfd, SOL_ALG, ALG_SET_AEAD_ASSOCLEN, assocdata, info->max_auth_size) < 0) {
			free(assocdata);
			if (errno == ENOPROTOOPT) {
				rc = EXIT_SUCCESS;
				goto err;
			}
			pr_fail_err("setsockopt");
			rc = EXIT_FAILURE;
			goto err;
		}
		free(assocdata);
#else
		/* Not supported, skip */
		rc = EXIT_SUCCESS;
		goto err;
#endif
	}

	fd = accept(sockfd, NULL, 0);
	if (fd < 0) {
		pr_fail_err("accept");
		rc = EXIT_FAILURE;
		goto err;
	}

	for (j = 32; j < (ssize_t)DATA_LEN; j += 32) {
		__u32 *u32ptr;
		struct msghdr msg;
		struct cmsghdr *cmsg;
		struct af_alg_iv *iv;	/* Initialisation Vector */
		struct iovec iov;

		if (!keep_stressing())
			break;
		(void)memset(&msg, 0, sizeof(msg));
		(void)memset(cbuf, 0, cbuf_size);

		msg.msg_control = cbuf;
		msg.msg_controllen = cbuf_size;

		/* Chosen operation - ENCRYPT */
		cmsg = CMSG_FIRSTHDR(&msg);
		/* Keep static analysis happy */
		if (!cmsg) {
			pr_fail_err("null cmsg");
			rc = EXIT_FAILURE;
			goto err_close;
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
		stress_strnrnd(input, DATA_LEN);
		iov.iov_base = input;
		iov.iov_len = DATA_LEN;

		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		if (sendmsg(fd, &msg, 0) < 0) {
			if (errno == ENOMEM)
				break;
			if (errno == EINVAL) {
				stress_af_alg_ignore(args, info);
				break;
			}
			pr_fail("%s: sendmsg using %s failed: errno=%d (%s)\n",
				args->name, info->name,
				errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto err_close;
		}
		if (read(fd, output, DATA_LEN) != DATA_LEN) {
			pr_fail("%s: read using %s failed: errno=%d (%s)\n",
				args->name, info->name,
				errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto err_close;
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
		iov.iov_len = DATA_LEN;

		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		if (sendmsg(fd, &msg, 0) < 0) {
			if (errno == ENOMEM)
				break;
			if (errno == EINVAL) {
				stress_af_alg_ignore(args, info);
				break;
			}
			pr_fail("%s: sendmsg using %s failed: errno=%d (%s)\n",
				args->name, info->name,
				errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto err_close;
		}
		if (read(fd, output, DATA_LEN) != DATA_LEN) {
			pr_fail("%s: read using %s failed: errno=%d (%s)\n",
				args->name, info->name,
				errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto err_close;
		} else {
			if (memcmp(input, output, DATA_LEN)) {
				pr_err("%s: decrypted data "
					"different from original data "
					"using %s\n",
					args->name, info->name);
			}
		}
	}

	rc = EXIT_SUCCESS;
	inc_counter(args);
err_close:
	(void)close(fd);
err:
	free(cbuf);
	free(output);
	free(input);

	return rc;
}

static int stress_af_alg_rng(
	const stress_args_t *args,
	const int sockfd,
	stress_crypto_info_t *info)
{
	int fd, rc;
	ssize_t j;
	struct sockaddr_alg sa;
	int retries = MAX_AF_ALG_RETRIES_BIND;
	char *output;
	const ssize_t output_size = 16;

	output = malloc(output_size + ALLOC_SLOP);
	if (!output)
		return EXIT_NO_RESOURCE;

	(void)memset(&sa, 0, sizeof(sa));
	sa.salg_family = AF_ALG;
	(void)shim_strlcpy((char *)sa.salg_type, "rng", sizeof(sa.salg_type));
	(void)shim_strlcpy((char *)sa.salg_name, info->name, sizeof(sa.salg_name) - 1);

retry_bind:
	if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		/* Perhaps the rng does not exist with this kernel */
		if ((errno == ENOENT) || (errno == EBUSY)) {
			rc = EXIT_SUCCESS;
			goto err;
		}
		if (errno == ETIMEDOUT && retries-- > 0)
			goto retry_bind;
		pr_fail_err("bind");
		rc = EXIT_FAILURE;
		goto err;
	}

	fd = accept(sockfd, NULL, 0);
	if (fd < 0) {
		pr_fail_err("accept");
		rc = EXIT_FAILURE;
		goto err;
	}

	for (j = 0; j < 16; j++) {
		if (!keep_stressing())
			break;
		if (read(fd, output, output_size) != output_size) {
			if (errno != EINVAL) {
				pr_fail_err("read");
				rc = EXIT_FAILURE;
				goto err_close;
			}
		}
		inc_counter(args);
		if (args->max_ops && (get_counter(args) >= args->max_ops)) {
			rc = EXIT_SUCCESS;
			goto err_close;
		}
	}
	rc = EXIT_SUCCESS;

err_close:
	(void)close(fd);
err:
	free(output);
	return rc;
}

static int stress_af_alg_count_crypto(void)
{
	int count = 0;
	stress_crypto_info_t *ci;

	/* Scan for duplications */
	for (ci = crypto_info_list; ci; ci = ci->next)
		count++;

	return count;
}

/*
 *  stress_af_alg_dump_crypto_list()
 *	dump crypto algorithm list to stdout
 */
static void stress_af_alg_dump_crypto_list(void)
{
	stress_crypto_info_t *ci;

	for (ci = crypto_info_list; ci; ci = ci->next) {
		if (ci->internal)
			continue;
		fprintf(stdout, "{ .crypto_type = %s, .type = \"%s\", .name = \"%s\"",
			type_to_name(ci->crypto_type), ci->type, ci->name);
		if (ci->block_size)
			fprintf(stdout, ",\t.block_size = %d",
				ci->block_size);
		if (ci->max_key_size)
			fprintf(stdout, ",\t.max_key_size = %d",
				ci->max_key_size);
		if (ci->max_auth_size)
			fprintf(stdout, ",\t.max_auth_size = %d",
				ci->max_auth_size);
		if (ci->iv_size)
			fprintf(stdout, ",\t.iv_size = %d",
				ci->iv_size);
		if (ci->digest_size)
			fprintf(stdout, ",\t.digest_size = %d",
				ci->digest_size);
		fprintf(stdout, " },\n");
	}
	fflush(stdout);
}

/*
 *  stress_af_alg()
 *	stress socket AF_ALG domain
 */
static int stress_af_alg(const stress_args_t *args)
{
	int sockfd = -1, rc = EXIT_FAILURE;
	int retries = MAX_AF_ALG_RETRIES;
	int count = stress_af_alg_count_crypto();
	bool af_alg_dump = false;

	(void)stress_get_setting("af-alg-dump", &af_alg_dump);

	if (af_alg_dump && args->instance == 0) {
		pr_inf("%s: dumping cryptographic algorithms found in /proc/crypto to stdout\n",
			args->name);
		stress_af_alg_dump_crypto_list();
	}

	if (args->instance == 0) {
		pr_inf("%s: %d cryptographic algorithms found in /proc/crypto\n",
			args->name, count);
	}

	stress_af_alg_add_crypto_defconfigs();
	count = stress_af_alg_count_crypto();

	if (args->instance == 0) {
		pr_inf("%s: %d cryptographic algorithms max (with defconfigs)\n",
			args->name, count);
	}

	for (;;) {
		sockfd = socket(AF_ALG, SOCK_SEQPACKET, 0);
		if (sockfd >= 0)
			break;

		retries--;
		if ((!keep_stressing_flag()) || (retries < 0) || (errno != EAFNOSUPPORT)) {
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
		stress_crypto_info_t *info;

		for (info = crypto_info_list; keep_stressing() && info; info = info->next) {
			if (info->internal || info->ignore)
				continue;
			switch (info->crypto_type) {
			case CRYPTO_AHASH:
			case CRYPTO_SHASH:
				rc = stress_af_alg_hash(args, sockfd, info);
				(void)rc;
				break;
#if defined(ALG_SET_AEAD_ASSOCLEN)
			case CRYPTO_AEAD:
#endif
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
 *  bool_field()
 *	parse a boolean from a string field
 *	error/default is false.
 */
static bool bool_field(const char *buffer)
{
	char *ptr = strchr(buffer, ':');

	if (!ptr)
		return false;
	if (!strncmp("yes", ptr + 2, 3))
		return true;
	if (!strncmp("no", ptr + 2, 2))
		return false;

	return false;
}

/*
 *  stress_af_alg_add_crypto()
 *	add crypto algorithm to list if it is unique
 */
static void stress_af_alg_add_crypto(stress_crypto_info_t *info)
{
	stress_crypto_info_t *ci;

	/* Don't add info with empty text fields */
	if ((info->name == NULL) || (info->type == NULL))
		return;

	/* Scan for duplications */
	for (ci = crypto_info_list; ci; ci = ci->next) {
		if (strcmp(ci->name, info->name) == 0 &&
		    strcmp(ci->type, info->type) == 0 &&
		    ci->block_size == info->block_size &&
		    ci->max_key_size == info->max_key_size &&
		    ci->max_auth_size == info->max_auth_size &&
		    ci->iv_size == info->iv_size &&
		    ci->digest_size == info->digest_size &&
		    ci->internal == info->internal)
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
 *  stress_af_alg_add_crypto_defconfigs()
 *	add crypto algorithm predefined/default configs to list
 */
static void stress_af_alg_add_crypto_defconfigs(void)
{
	unsigned int i;

	for (i = 0; i < SIZEOF_ARRAY(crypto_info_defconfigs); i++)
		stress_af_alg_add_crypto(&crypto_info_defconfigs[i]);
}

/*
 *  stress_af_alg_info_free()
 *	free and clear stress_crypto_info_t data
 */
static void stress_af_alg_info_free(stress_crypto_info_t *info)
{
	if (info->name)
		free(info->name);
	if (info->type)
		free(info->type);
}

/*
 *  stress_af_alg_init()
 *	populate cryto info list from data from /proc/crypto
 */
static void stress_af_alg_init(void)
{
	FILE *fp;
	char buffer[1024];
	stress_crypto_info_t info;

	crypto_info_list = NULL;
	fp = fopen("/proc/crypto", "r");
	if (!fp)
		return;

	(void)memset(buffer, 0, sizeof(buffer));
	(void)memset(&info, 0, sizeof(info));

	while (fgets(buffer, sizeof(buffer) - 1, fp)) {
		if (!strncmp(buffer, "name", 4)) {
			if (info.name)
				free(info.name);
			info.name = dup_field(buffer);
		}
		else if (!strncmp(buffer, "type", 4)) {
			info.crypto_type = name_to_type(buffer);
			if (info.type)
				free(info.type);
			info.type = dup_field(buffer);
		}
		else if (!strncmp(buffer, "blocksize", 9))
			info.block_size = int_field(buffer);
		else if (!strncmp(buffer, "max keysize", 11))
			info.max_key_size = int_field(buffer);
		else if (!strncmp(buffer, "maxauthsize", 11))
			info.max_auth_size = int_field(buffer);
		else if (!strncmp(buffer, "ivsize", 6))
			info.iv_size = int_field(buffer);
		else if (!strncmp(buffer, "digestsize", 10))
			info.digest_size = int_field(buffer);
		else if (!strncmp(buffer, "internal", 8))
			info.internal = bool_field(buffer);
		else if (buffer[0] == '\n') {
			if (info.crypto_type != CRYPTO_UNKNOWN)
				stress_af_alg_add_crypto(&info);
			else
				stress_af_alg_info_free(&info);
			(void)memset(&info, 0, sizeof(info));
		}
	}
	stress_af_alg_info_free(&info);

	(void)fclose(fp);
}

/*
 *  stress_af_alg_deinit()
 *	free crypto info list
 */
static void stress_af_alg_deinit(void)
{
	stress_crypto_info_t *ci = crypto_info_list;

	while (ci) {
		stress_crypto_info_t *next = ci->next;

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
	.opt_set_funcs = opt_set_funcs,
	.help = help
};

#else
stressor_info_t stress_af_alg_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
