/*
 * Copyright (C) 2025      NVidia.
 * Copyright (C) 2025      Colin Ian King.
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
#include "core-resctrl.h"
#include "core-setting.h"
#include "core-stressors.h"
#include <ctype.h>
#include <sys/mount.h>

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_ARM)
#define HAVE_RESCTRL
#endif

/*
 *  sudo stress-ng --resctrl p1=1:l3:1:10,p2=1:ffe:100,stream=0-1@p1,stream=2@p2 --stream 3 --stream-l3-size 16M -t 10 --metrics -v
 *
 *   defines: resctrl p1 to be node 1, L3 cache, bitmask 1, bandwith 10
 *            resctrl p2 to be node 1, no cache specified (defaults to L3), bitmask ffe, bandwidth 100
 *            stream instances 0 to 1 use profile p1
 *            stream instance 2 use profile p2
 */

#define STRESSOR_RESCTRL(name) 	NULL,

/*
 *  resctrl cache partitioning info
 */
typedef struct stress_partition_info {
	struct stress_partition_info *next;	/* next item in linked list */
	char *name;				/* name of partition, e.g. p1, p2.. */
	uint32_t partnum;			/* partition number, eg. p1 -> 1 */
	uint32_t cachelevel;			/* L1/L2/L3 cachelevel */
	uint32_t node;				/* node number */
	uint64_t bitmask;			/* hex bit mask */
	uint32_t bandwidth;			/* bandwidth, > 0 */
} stress_partition_info_t;

typedef struct stress_resctrl_info {
	struct stress_resctrl_info *next;	/* next item in list */
	char *name;				/* name of stressor */
	uint32_t begin;				/* begin instance range */
	uint32_t end;				/* end instance range */
	stress_partition_info_t *partition;
} stress_resctrl_info_t;

static stress_partition_info_t *stress_partition_head;

/* array of resctrl lists, 1 per stressor */
static stress_resctrl_info_t *stress_resctrls[] = {
	STRESSORS(STRESSOR_RESCTRL)
};

static uint32_t stress_resctrls_added;		/* > 0 if resctrls have been added */
#if defined(HAVE_RESCTRL)
static char resctrl_mnt[PATH_MAX];		/* resctrl mount point */
static bool resctrl_cleanup;			/* set true if resctrl needs cleaning up */
static bool resctrl_enabled;			/* set true if resctrl is enabled */
#endif

/*
 *  stress_parse_instance()
 *	parse positive stressor instance number
 */
static int stress_resctrl_parse_instance(
	const char *name,
	const char *str,
	uint32_t *val)
{
	int32_t ival;

	if (sscanf(str, "%" SCNd32, &ival) != 1) {
		*val = 0;
		fprintf(stderr, "resctrl: %s: invalid instance number: '%s'\n", name, str);
		return -1;
	}
	if ((ival < 0) || (ival >= STRESS_PROCS_MAX)) {
		fprintf(stderr, "resctrl: %s: instance number '%s' out of range 0..%d\n",
			name, str, STRESS_PROCS_MAX - 1);
		return -1;
	}
	*val = (uint32_t)ival;
	return 0;
}

/*
 *  stress_resctrl_err()
 *	report out of range instance values
 */
static void stress_resctrl_err(
	const char *name,
	const uint32_t begin,
	const uint32_t end)
{
	if (begin == end)
		fprintf(stderr, "resctrl: %s: duplicated instance %" PRIu32
			" in instance list\n", name, begin);
	else
		fprintf(stderr, "resctrl: %s: duplicated instances %" PRIu32 "-%" PRIu32
			" in instance list\n", name, begin, end);
}

/*
 *  stress_resctrl_add()
 *	add resctrl range and percentage for a stressor
 */
static int stress_resctrl_add(
	const char *name,
	stress_resctrl_info_t **head,
	const uint32_t begin,
	const uint32_t end,
	stress_partition_info_t *partition)
{
	stress_resctrl_info_t *resctrl;

	/* sanity check range */
	if (begin > end) {
		fprintf(stderr, "resctrl: %s: invalid range %" PRIu32 "-%"
			PRIu32 "\n", name, begin, end);
		return -1;
	}

	for (resctrl = *head; resctrl; resctrl = resctrl->next) {
		/* check for instance range overlaps with existing resctrls */
		if (begin == end) {
			if (begin >= resctrl->begin && begin <= resctrl->end) {
				stress_resctrl_err(name, begin, begin);
				return -1;
			}
			continue;
		}
		if (begin >= resctrl->begin && end <= resctrl->end) {
			stress_resctrl_err(name, begin, end);
			return -1;
		}

		if (begin >= resctrl->begin && begin <= resctrl->end) {
			stress_resctrl_err(name, begin, resctrl->end);
			return -1;
		}
		if (end >= resctrl->begin && end <= resctrl->end) {
			stress_resctrl_err(name, resctrl->end, end);
			return -1;
		}
		if (begin <= resctrl->begin && end >= resctrl->end) {
			stress_resctrl_err(name, resctrl->begin, resctrl->end);
			return -1;
		}
	}

	/* ..and add new resctrl */
	resctrl = (stress_resctrl_info_t *)calloc(1, sizeof(*resctrl));
	if (!resctrl) {
		fprintf(stderr, "out of memory parsing resctrl\n");
		return -1;
	}
	resctrl->name = strdup(name);
	if (!resctrl->name) {
		free(resctrl);
		fprintf(stderr, "out of memory parsing resctrl\n");
		return -1;
	}
	resctrl->begin = begin;
	resctrl->end = end;
	resctrl->next = *head;
	resctrl->partition = partition;
	*head = resctrl;

	stress_resctrls_added++;
	return 0;
}

/*
 *  stress_resctrl_check_index()
 *	sanity check index, should never fail
 */
static int stress_resctrl_check_index(ssize_t idx)
{
	/* should never fail, keep static analysis happy */
	if ((idx < 0) || (idx >= (ssize_t)SIZEOF_ARRAY(stress_resctrls))) {
		fprintf(stderr, "resctrl: internal error: out of range "
			"stressor index %zd\n", idx);
		return -1;
	}
	return 0;
}

/*
 *  stress_resctrl_parse_instance_list()
 *	parse instance list:
 *	    n1[,n2..] |
 *	    n1-n2[,n3..] |
 *	    all
 *	    ..or mix of these
 */
static int stress_resctrl_parse_instance_list(
	const ssize_t idx,
	char *list,
	const char *name,
	stress_partition_info_t *partition)
{
	char *ptr = list;
	char saved;

	if (stress_resctrl_check_index(idx) < 0)
		return -1;

	for (;;) {
		char *numptr = ptr;
		uint32_t begin, end;

		while (*ptr && (*ptr != ',' && *ptr != '-'))
			ptr++;
		if (*ptr == ',') {
			/* single value in comma list */
			*ptr++ = '\0';
			if (stress_resctrl_parse_instance(name, numptr, &begin) < 0)
				return -1;
			if (stress_resctrl_add(name, &stress_resctrls[idx],
						begin, begin, partition) < 0)
				return -1;
			if (!*ptr)
				return 0;
		} else if (*ptr == '-') {
			bool done = false;
			/* range in comma list */

			*ptr++ = '\0';
			if (stress_resctrl_parse_instance(name, numptr, &begin) < 0)
				return -1;
			numptr = ptr;
			while (*ptr && (*ptr != ','))
				ptr++;
			if (!*ptr)
				done = true;

			saved = *ptr;
			*ptr = '\0';
			if (stress_resctrl_parse_instance(name, numptr, &end) < 0)
				return -1;
			if (stress_resctrl_add(name, &stress_resctrls[idx],
						begin, end, partition) < 0)
				return -1;
			if (!saved || done)
				return 0;
			ptr++;
		} else {
			if (!*numptr)
				return 0;
			if (!*ptr) {
				if (!strcmp("all", numptr)) {
					if (stress_resctrl_add(name, &stress_resctrls[idx],
								0, STRESS_PROCS_MAX - 1, partition) < 0)
						return -1;
				} else {
					if (stress_resctrl_parse_instance(name, numptr, &begin) < 0)
						return -1;
					if (stress_resctrl_add(name, &stress_resctrls[idx],
							begin, begin, partition) < 0)
						return -1;
				}
				return 0;
			}
		}
	}
	return 0;
}

/*
 *  stress_resctrl_partition_find()
 *	find a resctrl partition by name, return NULL if not found
 */
static stress_partition_info_t *stress_resctrl_partition_find(const char *name)
{
	stress_partition_info_t *partition;

	for (partition = stress_partition_head; partition; partition = partition->next) {
		if (!strcmp(partition->name, name))
			return partition;
	}
	return NULL;
}

/*
 *  stress_resctrl_partition_add()
 *	add a new resctrl_partition
 */
static int stress_resctrl_partition_add(
	const char *name,
	const uint32_t partnum,
	const uint32_t cachelevel,
	const uint32_t node,
	const uint64_t bitmask,
	const uint32_t bandwidth)
{
	stress_partition_info_t *partition = stress_resctrl_partition_find(name);

	if (partition) {
		fprintf(stderr, "resctrl: duplicated partition name '%s'\n", name);
		return -1;
	}
	partition = calloc(1, sizeof(*partition));
	if (!partition) {
		fprintf(stderr, "out of memory parsing resctrl\n");
		return -1;
	}
	partition->name = strdup(name);
	if (!partition->name) {
		fprintf(stderr, "out of memory parsing resctrl\n");
		free(partition);
		return -1;
	}
	partition->partnum = partnum;
	partition->cachelevel = cachelevel;
	partition->node = node;
	partition->bitmask = bitmask;
	partition->bandwidth = bandwidth;

	partition->next = stress_partition_head;
	stress_partition_head = partition;

	return 0;
}

/*
 *  stress_resctrl_parse_partition()
 *	parse pN=1:[l3:]fff:20[,] where pN is alreading in name
 *
 */
static int stress_resctrl_parse_partition(const char *name, char **str)
{
	char *ptr = *str, *tmp;
	uint32_t node, cachelevel, bandwidth, partnum;
	uint64_t bitmask;
	int32_t val;

	if (sscanf(name + 1, "%" SCNd32, &val) != 1) {
		fprintf(stderr, "resctrl: invalid partition number in name '%s'\n", name);
		return -1;
	}
	if (val < 0) {
		fprintf(stderr, "resctrl: invalue negative partition value in '%s'\n", name);
		return -1;
	}
	partnum = (uint32_t)val;

	/* scan cache node */
	tmp = ptr;
	if (*ptr == '-')
		ptr++;
	while (*ptr && isdigit((int)*ptr))
		ptr++;
	if (*ptr != ':') {
		fprintf(stderr, "resctrl: missing ':' after cache node, got '%c' instead\n", *ptr);
		return -1;
	}
	if (*tmp == '\0') {
		fprintf(stderr, "resctrl: invalid cache node for partition '%s'\n", name);
		return -1;
	}
	*ptr++ = '\0';
	if (sscanf(tmp, "%" SCNd32, &val) != 1) {
		fprintf(stderr, "resctrl: invalid cache node '%s' for paritition '%s'\n", tmp, name);
		return -1;
	}
	if (val < 0) {
		fprintf(stderr, "resctrl: invalid negative cache node '%s' value for partition '%s'\n", tmp, name);
		return -1;
	}
	node = (uint32_t)val;

	/* scan optional level, L0 or l0 = use default cache level */
	if (*ptr == 'l' || *ptr == 'L') {
		ptr++;
		tmp = ptr;
		if (*ptr == '-')
			ptr++;
		while (*ptr && isdigit((int)*ptr))
			ptr++;
		if (*ptr != ':') {
			fprintf(stderr, "resctrl: missing ':' after cache level for partition '%s\n", name);
			return -1;
		}
		*ptr = '\0';
		if (*tmp == '\0') {
			fprintf(stderr, "resctrl: invalid cache level for partition '%s\n", name);
			return -1;
		}
		if (sscanf(tmp, "%" PRId32, &val) != 1) {
			fprintf(stderr, "resctrl: invalid cachelevel '%s' for paritition '%s'\n", tmp, name);
			return -1;
		}
		if ((val < 0) || (val > 3)) {
			fprintf(stderr, "resctrl: invalid cachelevel '%s' for paritition '%s' (expected L1..L3)\n", tmp, name);
			return -1;
		}
		cachelevel = (uint32_t)val;
		ptr++;
	} else {
		cachelevel = 0;
	}

	/* scan hexbitmask */
	tmp = ptr;
	while (*ptr && isxdigit((int )*ptr))
		ptr++;
	if (*ptr != ':') {
		fprintf(stderr, "resctrl: missing ':' after hex bitmask for partition '%s\n", name);
		return -1;
	}
	*ptr = '\0';
	if (*tmp == '\0') {
		fprintf(stderr, "resctrl: invalid cache hex bitmask for partition '%s'\n", name);
		return -1;
	}
	ptr++;
	if (sscanf(tmp, "%" PRIx64 , &bitmask) != 1) {
		fprintf(stderr, "resctrl: invalid cache hex bitmask '%s' for paritition '%s'\n", tmp, name);
		return -1;
	}

	/* scan bandwidth */
	tmp = ptr;
	if (*ptr == '-')
		ptr++;
	while (*ptr && isdigit((int)*ptr))
		ptr++;
	if (*ptr != ',') {
		fprintf(stderr, "resctrl: expecting ',' after bandwdith for partition '%s'\n", name);
		return -1;
	}
	if (sscanf(tmp, "%" PRId32, &val) != 1) {
		fprintf(stderr, "resctrl: invalid bandwidth '%s' for paritition '%s'\n", tmp, name);
		return -1;
	}
	if (val < 1) {
		fprintf(stderr, "resctrl: invalid bandwidth '%s' for partition '%s' (must be > 0)\n", tmp, name);
		return -1;
	}
	bandwidth = (uint32_t)val;

	ptr++;
	*str = ptr;

	return stress_resctrl_partition_add(name, partnum, cachelevel, node, bitmask, bandwidth);
}

/*
 *  stress_resctrl_parse()
 *	parse resctrl option
 */
int stress_resctrl_parse(char *opt_resctrl)
{
	char *str = strdup(opt_resctrl);
	char *ptr;

	if (!str) {
		fprintf(stderr, "out of memory parsing resctrl\n");
		return -1;
	}

	for (ptr = str;;) {
		char *name, *instances;
		char *partition_name;
		stress_partition_info_t *partition = NULL;
		char saved;
		ssize_t idx;

		name = ptr;
		/* scan to get name field */
		while (*ptr && *ptr != '=')
			ptr++;

		if (*name == '\0') {
			fprintf(stderr, "resctrl: invalid empty name\n");
			free(str);
			return -1;
		}

		if (*ptr != '=') {
			fprintf(stderr, "resctrl: expecting '=' delimiter "
				"after stressor name '%s'\n", name);
			free(str);
			return -1;
		}
		*ptr++ = '\0';

		if ((strlen(name) >= 2) && (name[0] == 'p') && isdigit((int)(name[1]))) {
			if (stress_resctrl_parse_partition(name, &ptr) < 0) {
				free(str);
				return -1;
			}
			continue;
		}

		idx = stress_stressor_find(name);
		if (idx < 0) {
			fprintf(stderr, "invalid stressor name '%s'\n", name);
			free(str);
			return -1;
		}

		/* parse cpu=0-1@p1,2-3@p2,*=p2 */
		instances = ptr;
		/* scan to get list field */
		while (*ptr && *ptr != '@')
			ptr++;
		if (*ptr != '@') {
			fprintf(stderr, "resctrl: expecting '@' delimiter "
				"after instances list '%s'\n", name);
			free(str);
			return -1;
		}
		*ptr++ = '\0';

		/* scan for partition name */
		if (*ptr != 'p') {
			fprintf(stderr, "resctrl: missing partition name after '@' delimimiter\n");
			free(str);
			return -1;
		}

		partition_name = ptr;
		ptr++;
		while (*ptr && isdigit((int)*ptr))
			ptr++;

		saved = *ptr;
		*ptr = '\0';

		partition = stress_resctrl_partition_find(partition_name);
		if (!partition) {
			fprintf(stderr, "resctrl: undefined partition name '%s'\n", partition_name);
			free(str);
			return -1;
		}
		*ptr = saved;
		if (stress_resctrl_parse_instance_list(idx, instances, name, partition) < 0) {
			free(str);
			return -1;
		}

		if (*ptr == '\0') {
			free(str);
			return 0;
		}

		if (*ptr != ',') {
			fprintf(stderr, "resctrl: got '%c', but expecting ',' for next stressor in list\n", *ptr);
			free(str);
			return -1;
		}
		ptr++;
	}
	return 0;
}

#if defined(HAVE_RESCTRL)
/*
 *  stress_resctrl_set_pid()
 *	set the resctrl for a specific process based on partition info
 */
static int stress_resctrl_set_pid(const char *name, const pid_t pid, stress_partition_info_t *partition)
{
	char buf[64];
	char path[PATH_MAX + 64];
	ssize_t ret;
	char *ptr;

	(void)snprintf(path, sizeof(path), "%s/stress-ng-%s/schemata", resctrl_mnt, partition->name);
	if (partition->cachelevel == 0) {
		ret = stress_system_read(path, buf, sizeof(buf));
		if (ret < 0) {
			pr_warn("%s: failed to read default schemata cache level for resctrl partition '%s', errno=%d (%s)\n",
				name, partition->name, errno, strerror(errno));
			return -1;
		}
		ptr = strstr(buf, "L");
		if (ptr) {
			uint32_t cachelevel;

			if (sscanf(ptr + 1, "%" SCNu32, &cachelevel) < 1) {
				pr_warn("%s: failed to parse default schemata cache level for resctrl partition '%s', errno=%d (%s)\n",
					name, partition->name, errno, strerror(errno));
				return -1;
			}
			partition->cachelevel = cachelevel;
		}
	}
	(void)snprintf(buf, sizeof(buf), "L%" PRIu32 ":%" PRIu32 "=%" PRIx64 "\n",
		partition->cachelevel, partition->node, partition->bitmask);
	ret = stress_system_write(path, buf, strlen(buf));
	if (ret < 0) {
		ptr = strstr(buf, "\n");
		if (ptr)
			*ptr = '\0';
		pr_warn("%s: failed to set schemata '%s' for resctrl partition '%s', errno=%d (%s)\n",
			name, buf, partition->name, errno, strerror(errno));
	}

	(void)snprintf(buf, sizeof(buf), "MB:%" PRIu32 "=%" PRIu32 "\n",
		partition->node, partition->bandwidth);
	ret = stress_system_write(path, buf, strlen(buf));
	if (ret < 0) {
		ptr = strstr(buf, "\n");
		if (ptr)
			*ptr = '\0';
		pr_warn("%s: failed to set schemata '%s' for resctrl partition '%s', errno=%d (%s)\n",
			name, buf, partition->name, errno, strerror(errno));
	}

	(void)snprintf(path, sizeof(path), "%s/stress-ng-%s/tasks", resctrl_mnt, partition->name);
	(void)snprintf(buf, sizeof(buf), "%" PRIdMAX "\n", (intmax_t)pid);
	ret = stress_system_write(path, buf, strlen(buf));
	if (ret < 0) {
		ptr = strstr(buf, "\n");
		if (ptr)
			*ptr = '\0';
		pr_warn("%s: failed to add pid %" PRIdMAX " to resctrl partition '%s', errno=%d (%s)\n",
			name, (intmax_t)pid, partition->name, errno, strerror(errno));
	}
	pr_dbg("%s: resctrl: set PID %" PRIdMAX " to %s L%" PRIu32 ":%" PRIu32 "=%" PRIx64 " MB:%" PRIu32 "=%" PRIu32 "\n",
			name, (intmax_t)pid, partition->name, partition->cachelevel,
			partition->node, partition->bitmask,  partition->node, partition->bandwidth);
	return 0;
}
#endif

/*
 *  stress_resctrl_set()
 *	set stressor resctrl given the name of the stressor, it's instance and PID
 */
int stress_resctrl_set(const char *name, const uint32_t instance, const pid_t pid)
{
#if defined(HAVE_RESCTRL)
	stress_resctrl_info_t *resctrl;
	ssize_t idx;

	if (!resctrl_enabled || !stress_resctrls_added)
		return 0;

	idx = stress_stressor_find(name);
	if (stress_resctrl_check_index(idx) < 0)
		return -1;

	for (resctrl = stress_resctrls[idx]; resctrl; resctrl = resctrl->next) {
		if ((instance >= resctrl->begin) && (instance <= resctrl->end))
			return stress_resctrl_set_pid(name, pid, resctrl->partition);
	}
#else
	(void)name;
	(void)instance;
	(void)pid;
#endif
	return 0;
}

/*
 *  stress_resctrl_init()
 * 	initialise resctrls
 */
void stress_resctrl_init(void)
{
#if defined(HAVE_RESCTRL)
	stress_partition_info_t *partition;
	char buf[1024];
	FILE *fp;

	resctrl_enabled = true;

	/* no resctrls specified, no need for init */
	if (!stress_resctrls_added) {
		resctrl_enabled = false;
		return;
	}

	/* try and found existing resctrl mount point */
	fp = fopen("/proc/mounts", "r");
	if (!fp) {
		pr_warn("resctl: cannot open /proc/mounts, errno=%d (%s), disabling resctrl\n", errno, strerror(errno));
		resctrl_enabled = false;
		return;
	}
	*resctrl_mnt = '\0';
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (strncmp(buf, "resctrl", 7))
			continue;
		if (sscanf(buf + 8, "%256s", resctrl_mnt) < 1)
			break;
	}
	(void)fclose(fp);

	if (*resctrl_mnt) {
		resctrl_cleanup = false;
	} else {
		/* no mount point, let stress-ng mount one */
		(void)snprintf(resctrl_mnt, sizeof(resctrl_mnt), "%s/stress-ng-resctrl", stress_get_temp_path());
		if (mkdir(resctrl_mnt, S_IRWXU) < 0) {
			pr_warn("resctrl: cannot create resctrl mount point, errno=%d (%s), disabling resctrl\n",
				errno, strerror(errno));
			resctrl_enabled = false;
			return;
		}

		/* sudo mount -t resctrl resctrl /sys/fs/resctrl */
		if (mount("resctrl", resctrl_mnt, "resctrl", 0, NULL) < 0) {
			pr_warn("resctl: cannot mount resctl, errno=%d (%s), disabling resctrl\n", errno, strerror(errno));
			(void)rmdir(resctrl_mnt);
			resctrl_enabled = false;
			return;
		}
		resctrl_cleanup = true;
	}

	/*
	 *  attempt to create resctrls
	 */
	for (partition = stress_partition_head; partition; partition = partition->next) {
		char path[PATH_MAX + 64];
		int fd;

		(void)snprintf(path, sizeof(path), "%s/stress-ng-%s", resctrl_mnt, partition->name);
		fd = mkdir(path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
		if (fd < 0) {
			if (errno == EEXIST)
				continue;
			pr_inf("resctrl: cannot create resctrl for %s, errno=%d (%s), disabling resctrl\n",
				partition->name, errno, strerror(errno));
			resctrl_enabled = false;
			break;
		}
	}
#else
	if (stress_resctrls_added)
		pr_inf("resctrl: feature not supported, ignoring resctrl settings\n");
#endif
}

/*
 *  stress_resctrl_deinit()
 *	de-initialize resctrls
 */
void stress_resctrl_deinit(void)
{
#if defined(HAVE_RESCTRL)
	size_t i;
	stress_partition_info_t *partition = stress_partition_head;

	/* remove resctrl partition directories */
	while (partition) {
		char path[PATH_MAX + 64];
		stress_partition_info_t *next = partition->next;

		(void)snprintf(path, sizeof(path), "%s/stress-ng-%s", resctrl_mnt, partition->name);
		(void)rmdir(path);

		free(partition->name);
		free(partition);
		partition = next;
	}
	stress_partition_head = NULL;

	/* free resctrls */
	for (i = 0; i < SIZEOF_ARRAY(stress_resctrls); i++) {
		stress_resctrl_info_t *resctrl = stress_resctrls[i];

		while (resctrl) {
			stress_resctrl_info_t *next = resctrl->next;

			free(resctrl->name);
			free(resctrl);
			resctrl = next;
		}
	}
	stress_resctrls_added = 0;

	/* if we had to create a mount point umount and remove it */
	if (resctrl_cleanup) {
		(void)umount(resctrl_mnt);
		(void)rmdir(resctrl_mnt);
	}

	resctrl_cleanup = false;
	*resctrl_mnt = '\0';
	resctrl_enabled = false;
#endif
}
