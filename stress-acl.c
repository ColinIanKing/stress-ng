/*
 * Copyright (C) 2023      Colin Ian King.
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

static const stress_help_t help[] = {
	{ NULL,	"acl N",	"start N workers thrashing acl file mode bits " },
	{ NULL,	"acl-ops N",	"stop acl workers after N bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_ACL) &&	\
    defined(HAVE_SYS_ACL_H)

typedef struct {
	const int perm_mask;
} stress_acl_entry;

static acl_tag_t stress_acl_tags[] = {
	ACL_USER_OBJ,
	ACL_GROUP_OBJ,
	ACL_USER,
	ACL_GROUP,
	ACL_OTHER,
	ACL_MASK,
};

static stress_acl_entry stress_acl_entries[] = {
	{ 0 },
	{ ACL_READ },
	{ ACL_WRITE },
	{ ACL_EXECUTE },
	{ ACL_READ | ACL_WRITE },
	{ ACL_READ | ACL_EXECUTE },
	{ ACL_WRITE | ACL_EXECUTE },
	{ ACL_READ | ACL_WRITE | ACL_EXECUTE },
};

/*
 *  acl_delete_all()
 *	try to delete all acl entries on filename
 */
static void acl_delete_all(const char *filename)
{
	acl_t acl;
	acl_entry_t entry;
	int which = ACL_FIRST_ENTRY;

	acl = acl_get_file(filename, ACL_TYPE_ACCESS);
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
		acl_set_file(filename, ACL_TYPE_ACCESS, acl);
	acl_free(acl);
}

/*
 *  acl_set()
 *	exercise all valid an invalid acls on filename
 */
static int acl_set(stress_args_t *args, const char *filename)
{
	size_t usr, grp, oth;

	for (usr = 0; usr < SIZEOF_ARRAY(stress_acl_entries); usr++) {
		for (grp = 0; grp < SIZEOF_ARRAY(stress_acl_entries); grp++) {
			for (oth = 0; oth < SIZEOF_ARRAY(stress_acl_entries); oth++) {
				acl_t acl;
				acl_entry_t entry = (acl_entry_t)NULL;
				acl_permset_t permset;
				uid_t uid;
				gid_t gid;
				size_t j;

				if (!stress_continue(args))
					break;

				acl = acl_init(64);
				if (acl == (acl_t)NULL) {
					pr_inf("%s: failed to initialize acl, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					return EXIT_NO_RESOURCE;
				}

				for (j = 0; j < SIZEOF_ARRAY(stress_acl_tags); j++) {
					int perm_mask = 0;

					if (acl_create_entry(&acl, &entry) != 0) {
						pr_inf("%s: failed to create acl entry, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						acl_free(acl);
						return EXIT_FAILURE;
					}
					if (acl_set_tag_type(entry, stress_acl_tags[j]) != 0) {
						pr_inf("%s: failed to set tag type, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						acl_free(acl);
						return EXIT_FAILURE;
					}
					switch (stress_acl_tags[j]) {
					case ACL_USER_OBJ:
						perm_mask = stress_acl_entries[usr].perm_mask;
						break;
					case ACL_USER:
						uid = getuid();
						acl_set_qualifier(entry, &uid);
						perm_mask = stress_acl_entries[usr].perm_mask;
						break;
					case ACL_GROUP_OBJ:
						perm_mask = stress_acl_entries[grp].perm_mask;
						break;
					case ACL_GROUP:
						gid = getgid();
						acl_set_qualifier(entry, &gid);
						perm_mask = stress_acl_entries[grp].perm_mask;
						break;
					case ACL_OTHER:
						perm_mask = stress_acl_entries[oth].perm_mask;
						break;
					case ACL_MASK:
						perm_mask = 0777;
						break;
					}

					if (acl_get_permset(entry, &permset) != 0) {
						pr_inf("%s: failed to get permset, errno=%d (%s)\n",
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
						pr_inf("%s: failed to set permissions, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						return EXIT_FAILURE;
					}
					acl_calc_mask(&acl);

					if (acl_valid(acl) == 0) {
						stress_bogo_inc(args);
						if (acl_set_file(filename, ACL_TYPE_ACCESS, acl) != 0) {
							switch (errno) {
							case EOPNOTSUPP:
								pr_inf_skip("%s: cannot set acl on '%s', errno=%d (%s), skipping stressor\n",
									args->name, filename, errno, strerror(errno));
								acl_free(acl);
								return EXIT_NOT_IMPLEMENTED;
							case ENOENT:
								acl_free(acl);
								return EXIT_SUCCESS;
							default:
								pr_inf("%s: failed to set acl on '%s', errno=%d (%s)\n",
									args->name, filename, errno, strerror(errno));
								return EXIT_FAILURE;
							}
						}
					}
				}
				acl_free(acl);
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
	int fd, rc = EXIT_FAILURE;

	char filename[PATH_MAX], pathname[PATH_MAX], longpath[PATH_MAX + 16];

	stress_temp_dir_args(args, pathname, sizeof(pathname));
	if (mkdir(pathname, S_IRUSR | S_IRWXU) < 0) {
		if (errno != EEXIST) {
			rc = stress_exit_status(errno);
			pr_fail("%s: mkdir %s failed, errno=%d (%s)\n",
				args->name, pathname, errno, strerror(errno));
			return rc;
		}
	}

	stress_rndstr(longpath, sizeof(longpath));
	longpath[0] = '/';

	(void)stress_temp_filename_args(args, filename, sizeof(filename), stress_mwc32());
	if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: create %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto tidy;
	}
	(void)close(fd);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		acl_delete_all(filename);
		rc = acl_set(args, filename);
		if (rc != EXIT_SUCCESS)
			break;
	} while (stress_continue(args));
	acl_delete_all(filename);
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)shim_unlink(filename);
	(void)shim_rmdir(pathname);

	return rc;
}

stressor_info_t stress_acl_info = {
	.stressor = stress_acl,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_acl_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#endif
