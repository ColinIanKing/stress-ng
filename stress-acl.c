/*
 * Copyright (C) 2024      Colin Ian King.
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

#if defined(HAVE_SYS_ACL_H)
#include <sys/acl.h>
#endif
#if defined(HAVE_ACL_LIBACL_H)
#include <acl/libacl.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"acl N",	"start N workers exercising valid ACL file mode bits " },
	{ NULL,	"acl-rand",	"randomize ordering of ACL file mode tests" },
	{ NULL,	"acl-ops N",	"stop acl workers after N bogo operations" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_acl_rand, "acl-rand", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_LIB_ACL) &&		\
    defined(HAVE_ACL_LIBACL_H) &&	\
    defined(HAVE_SYS_ACL_H)

static const acl_tag_t stress_acl_tags[] = {
	ACL_USER_OBJ,
	ACL_GROUP_OBJ,
	ACL_USER,
	ACL_GROUP,
	ACL_OTHER,
};

static const int stress_acl_entries[] = {
	0,
	ACL_READ,
	ACL_WRITE,
	ACL_EXECUTE,
	ACL_READ | ACL_WRITE,
	ACL_READ | ACL_EXECUTE,
	ACL_WRITE | ACL_EXECUTE,
	ACL_READ | ACL_WRITE | ACL_EXECUTE,
};

static const acl_type_t stress_acl_types[] = {
	ACL_TYPE_ACCESS,
	ACL_TYPE_DEFAULT,
};


/*
 *  stress_acl_delete_all()
 *	try to delete all acl entries on filename
 */
static inline void stress_acl_delete_all(const char *filename, const acl_type_t type)
{
	acl_t acl;
	acl_entry_t entry;
	int which = ACL_FIRST_ENTRY;

	acl = acl_get_file(filename, type);
	if (acl == (acl_t)NULL)
		return;

	for (;;) {
		int ret;

		ret = acl_get_entry(acl, which, &entry);
		if (ret <= 0)
			break;
		(void)acl_delete_entry(acl, entry);
		which = ACL_NEXT_ENTRY;
	}
	if (acl_valid(acl) == 0)
		acl_set_file(filename, type, acl);
	acl_free(acl);
}

/*
 *  stress_acl_cmp()
 *	naive acl comparison, assumes that acl_to_text generates
 *	the same strings for identical acls
 */
static inline int stress_acl_cmp(acl_t acl1, acl_t acl2)
{
	char *acl_txt1, *acl_txt2;
	ssize_t len1, len2;
	int ret = -1;

	acl_txt1 = acl_to_text(acl1, &len1);
	if (!acl_txt1)
		return 0;
	acl_txt2 = acl_to_text(acl2, &len2);
	if (!acl_txt2) {
		acl_free((void *)acl_txt1);
		return 0;
	}
	if (len1 == len2)
		ret = strcmp(acl_txt1, acl_txt2);

	acl_free((void *)acl_txt2);
	acl_free((void *)acl_txt1);

	return ret;
}

/*
 *  stress_acl_perms()
 *	convert ACL permission bits to string
 */
static void stress_acl_perms(acl_t acl, char *str, const size_t str_len)
{
	int which = ACL_FIRST_ENTRY;

	if (str_len < 18)
		return;

	/* user, group and other permission bits */
	(void)snprintf(str, str_len, "u:--- g:--- o:---");

	for (;;) {
		acl_tag_t tag;
		acl_entry_t entry;
		acl_permset_t permset;
		int idx = 0;

		if (acl_get_entry(acl, which, &entry) <= 0)
			break;
		which = ACL_NEXT_ENTRY;

		if (acl_get_tag_type(entry, &tag) != 0)
			continue;
		if (acl_get_permset(entry, &permset) != 0)
			continue;

		switch (tag) {
		case ACL_USER:
			idx = 2;
			break;
		case ACL_GROUP:
			idx = 8;
			break;
		case ACL_OTHER:
			idx = 14;
			break;
		default:
			continue;
		}

		if (acl_get_perm(permset, ACL_READ))
			str[idx + 0] = 'r';
		if (acl_get_perm(permset, ACL_WRITE))
			str[idx + 1] = 'w';
		if (acl_get_perm(permset, ACL_EXECUTE))
			str[idx + 2] = 'x';
	}
}

/*
 *  stress_acl_free()
 *	free ACLs
 */
static inline void stress_acl_free(acl_t *acls, const size_t acl_count)
{
	register size_t i;

	for (i = 0; i < acl_count; i++) {
		acl_free(acls[i]);
		acls[i] = NULL;
	}
}

/*
 *  stress_acl_setup()
 *	setup valid ACLs
 */
static int stress_acl_setup(
	stress_args_t *args,
	const bool acl_rand,
	const uid_t uid,
	const gid_t gid,
	const size_t max_acls,
	acl_t *acls,
	size_t *acl_count)
{
	size_t usr, grp, oth;

	for (usr = 0; usr < SIZEOF_ARRAY(stress_acl_entries); usr++) {
		for (grp = 0; grp < SIZEOF_ARRAY(stress_acl_entries); grp++) {
			for (oth = 0; oth < SIZEOF_ARRAY(stress_acl_entries); oth++) {
				acl_t acl;
				acl_entry_t entry = (acl_entry_t)NULL;
				acl_permset_t permset;
				size_t j;

				acl = acl_init((int)SIZEOF_ARRAY(stress_acl_tags));
				if (acl == (acl_t)NULL) {
					pr_inf("%s: failed to initialize acl, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					return EXIT_NO_RESOURCE;
				}

				for (j = 0; j < SIZEOF_ARRAY(stress_acl_tags); j++) {
					int perm_mask = 0;

					if (acl_create_entry(&acl, &entry) != 0) {
						pr_fail("%s: failed to create acl entry, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						acl_free(acl);
						return EXIT_FAILURE;
					}
					if (acl_set_tag_type(entry, stress_acl_tags[j]) != 0) {
						pr_fail("%s: failed to set tag type, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						acl_free(acl);
						return EXIT_FAILURE;
					}
					switch (stress_acl_tags[j]) {
					case ACL_USER_OBJ:
						perm_mask = stress_acl_entries[usr];
						break;
					case ACL_USER:
						acl_set_qualifier(entry, &uid);
						perm_mask = stress_acl_entries[usr];
						break;
					case ACL_GROUP_OBJ:
						perm_mask = stress_acl_entries[grp];
						break;
					case ACL_GROUP:
						acl_set_qualifier(entry, &gid);
						perm_mask = stress_acl_entries[grp];
						break;
					case ACL_OTHER:
						perm_mask = stress_acl_entries[oth];
						break;
					case ACL_MASK:
						perm_mask = 0777;
						break;
					}

					if (acl_get_permset(entry, &permset) != 0) {
						pr_fail("%s: failed to get permset, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						return EXIT_FAILURE;
					}
					if (acl_clear_perms(permset) != 0)
						pr_inf("%s: failed to clear permissions\n", args->name);
					if (perm_mask & ACL_READ)
						acl_add_perm(permset, ACL_READ);
					if (perm_mask & ACL_WRITE)
						acl_add_perm(permset, ACL_WRITE);
					if (perm_mask & ACL_EXECUTE)
						acl_add_perm(permset, ACL_EXECUTE);
					if (acl_set_permset(entry, permset) != 0) {
						pr_fail("%s: failed to set permissions, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						return EXIT_FAILURE;
					}
					acl_calc_mask(&acl);
				}

				if (acl_valid(acl) == 0) {
					acls[*acl_count] = acl;
					(*acl_count)++;
					if (*acl_count >= max_acls)
						return EXIT_SUCCESS;
				} else {
					acl_free(acl);
					acl = (acl_t)NULL;
				}
			}
		}
	}

	if (acl_rand) {
		register size_t i;
		register size_t n = *acl_count;

		for (i = 0; i < n; i++) {
			register acl_t tmp;
			register size_t j = (size_t)stress_mwc32modn(n);

			tmp = acls[i];
			acls[i] = acls[j];
			acls[j] = tmp;
		}
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_acl_exercise()
 *	exercise all valid an invalid acls on filename
 */
static int stress_acl_exercise(
	stress_args_t *args,
	const char *filename,
	const acl_type_t type,
	acl_t *acls,
	const size_t acl_count,
	stress_metrics_t metrics[2])
{
	size_t i;

	for (i = 0; i < acl_count && stress_continue(args); i++) {
		const double t1 = stress_time_now();

		if (LIKELY(acl_set_file(filename, type, acls[i]) == 0)) {
			double t2 = stress_time_now();
			acl_t acl;

			metrics[0].count += 1.0;

			acl = acl_get_file(filename, type);
			if (acl) {
				metrics[1].duration += (stress_time_now() - t2);
				metrics[1].count += 1.0;

				if (stress_acl_cmp(acls[i], acl)) {
					char setacl[32], getacl[32];

					stress_acl_perms(acls[i], setacl, sizeof(setacl));
					stress_acl_perms(acl, getacl, sizeof(getacl));

					pr_fail("%s: mismatch between set acl %s and get acl %s\n",
						args->name, setacl, getacl);
					acl_free(acl);
					return EXIT_FAILURE;
				}
				acl_free(acl);
			}

			metrics[0].duration += (t2 - t1);
			metrics[0].count += 1.0;

			stress_bogo_inc(args);
		} else {
			char getacl[32];

			switch (errno) {
			case EOPNOTSUPP:
				pr_inf_skip("%s: cannot set acl on '%s', errno=%d (%s), skipping stressor\n",
					args->name, filename, errno, strerror(errno));
				return EXIT_NOT_IMPLEMENTED;
			case ENOENT:
			case EACCES:
				/* silently ignore these, faked to OK */
				return EXIT_SUCCESS;
			default:
				stress_acl_perms(acls[i], getacl, sizeof(getacl));
				pr_fail("%s: failed to set acl on '%s' %s, errno=%d (%s)\n",
					args->name, filename, getacl, errno, strerror(errno));
				return EXIT_FAILURE;
			}
		}
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_acl
 *	stress acl
 */
static int stress_acl(stress_args_t *args)
{
	double rate;
	int fd, rc;
	const uid_t uid = getuid();
	const gid_t gid = getgid();
	acl_t *acls;
	size_t i, acl_count = 0;
	bool acl_rand = false;
	const size_t max_acls = SIZEOF_ARRAY(stress_acl_entries) *
				      SIZEOF_ARRAY(stress_acl_entries) *
				      SIZEOF_ARRAY(stress_acl_entries) *
				      SIZEOF_ARRAY(stress_acl_tags);
	const size_t acls_size = max_acls * sizeof(*acls);
	stress_metrics_t metrics[2];
	char filename[PATH_MAX], pathname[PATH_MAX];
	static char *description[] = {
		"nanoseconds to set an ACL",
		"nanoseconds to get an ACL",
	};

	(void)stress_get_setting("acl-rand", &acl_rand);

	acls = (acl_t *)stress_mmap_populate(NULL, acls_size,
					PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE,
					-1, 0);
	if (acls == MAP_FAILED) {
		pr_inf("%s: cannot mmap %zd bytes for valid acl cache, errno=%d (%s), skipping stressor\n",
			args->name, acls_size, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(acls, acls_size, "acls");
	rc = stress_acl_setup(args, acl_rand, uid, gid, max_acls, acls, &acl_count);
	if (rc != EXIT_SUCCESS)
		goto tidy_unmap;

	stress_temp_dir_args(args, pathname, sizeof(pathname));
	if (mkdir(pathname, S_IRUSR | S_IRWXU) < 0) {
		if (errno != EEXIST) {
			rc = stress_exit_status(errno);
			pr_fail("%s: mkdir %s failed, errno=%d (%s)\n",
				args->name, pathname, errno, strerror(errno));
			goto tidy_acl_free;
		}
	}

	(void)stress_temp_filename_args(args, filename, sizeof(filename), stress_mwc32());
	if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: create %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto tidy;
	}
	(void)close(fd);

	for (i = 0; i < SIZEOF_ARRAY(metrics); i++) {
		metrics[i].duration = 0.0;
		metrics[i].count = 0.0;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	stress_sync_start_wait(args);

	rc = 0;
	do {
		for (i = 0; i < SIZEOF_ARRAY(stress_acl_types); i++) {
			const acl_type_t type = stress_acl_types[i];

			stress_acl_delete_all(filename, type);
			rc = stress_acl_exercise(args, filename, type, acls, acl_count, metrics);
			if (rc != EXIT_SUCCESS)
				break;
		}
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

	for (i = 0; i < SIZEOF_ARRAY(stress_acl_types); i++) {
		stress_acl_delete_all(filename, stress_acl_types[i]);
	}

	if (args->instance == 0)
		pr_inf("%s: %zd unique ACLs used\n", args->name, acl_count);

	for (i = 0; i < SIZEOF_ARRAY(metrics); i++) {
		rate = (metrics[i].count > 0.0) ? metrics[i].duration * STRESS_DBL_NANOSECOND / metrics[i].count : 0.0;
		stress_metrics_set(args, i, description[i], rate, STRESS_METRIC_HARMONIC_MEAN);
	}

	rc = EXIT_SUCCESS;
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)shim_unlink(filename);
	(void)shim_rmdir(pathname);

tidy_acl_free:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_acl_free(acls, acl_count);

tidy_unmap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap((void *)acls, acls_size);

	return rc;
}

stressor_info_t stress_acl_info = {
	.stressor = stress_acl,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_acl_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without libacl or acl/libacl.h or sys/acl.h"
};
#endif
