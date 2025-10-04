/*
 * Copyright (C) 2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-smart.h"

#include <ctype.h>
#include <sys/ioctl.h>

#if defined(HAVE_SCSI_SG_H)
#include <scsi/sg.h>
#endif

#if defined(HAVE_SCSI_SCSI_IOCTL_H)
#include <scsi/scsi_ioctl.h>
#endif

#define DEVS_MAX      		(256)

#define SENSE_BUF_SZ		(0x20)
#define BUF_SZ			(0x200)

#if defined(HAVE_SCSI_SG_H) &&	\
    defined(HAVE_SCSI_SCSI_IOCTL_H)
#define HAVE_SMART	(1)
#endif

/*
 *  See https://www.t10.org/ftp/t10/document.04/04-262r8.pdf
 */
#define CBD_OPERATION_CODE	(0xa1) /* Operation code */
#define CBD_PROTOCOL_DMA	(0x06) /* Protocol DMA */
#define CBD_T_LENGTH		(0x02) /* Tx len in SECTOR_COUNT field */
#define CBD_BYT_BLOK		(0x01) /* Tx len in byte blocks */
#define CBD_T_DIR		(0x01) /* Tx direction, device -> client */
#define CBD_CK_COND		(0x00) /* Check condition, disabled */
#define CBD_OFF_LINE		(0x00) /* offline time, 0 seconds */
#define CBD_FEATURES		(0xd0) /* feature: read smart data */
#define CBD_SECTOR_COUNT	(0x01) /* 1 sector to read */
#define CBD_LBA_LOW		(0x00) /* LBA: 0:7 N/A */
#define CBD_LBA_MID		(0x4f) /* LBA: 23:8 magic: 0xc24f */
#define CBD_LBA_HIGH		(0xc2)
#define CBD_DEVICE		(0x00) /* all zero */
#define CBD_COMMAND		(0xb0) /* command: read smart log */
#define CBD_RESVERVED		(0x00) /* N/A */
#define CBD_CONTROL		(0x00)

#define ATTR_FLAG_WARRANTY	(0x01)
#define ATTR_FLAG_OFFLINE	(0x02)
#define ATTR_FLAG_PERFORMANCE	(0x04)
#define ATTR_FLAG_ERROR_RATE	(0x08)
#define ATTR_FLAG_EVENT_COUNT	(0x10)
#define ATTR_FLAG_SELF_PRESERV	(0x20)

/* SMART log raw data value */
typedef struct __attribute__ ((packed)) {
	uint8_t		attr_id;
	uint16_t	attr_flags;
	uint8_t		current_value;
	uint8_t		worst_value;
	uint32_t	data;
	uint16_t	attr_data;
	uint8_t		threshold;
} stress_smart_raw_value_t;

typedef struct {
	size_t		count;
	size_t		size;
	stress_smart_raw_value_t values[];
} stress_smart_data_t;

typedef struct stress_smart_dev_t {
	char *dev_name;
	stress_smart_data_t *data_begin;
	stress_smart_data_t *data_end;
	struct stress_smart_dev_t *next;
} stress_smart_dev_t;

typedef struct {
	stress_smart_dev_t *dev;
} stress_smart_devs_t;

#if defined(HAVE_SMART)

static stress_smart_devs_t smart_devs;
/*
 *  S.M.A.R.T. ID Descriptions, see:
 *  https://en.wikipedia.org/wiki/Self-Monitoring,_Analysis_and_Reporting_Technology
 */
static const char * const id_str[256] = {
	[0x01] = "Read Error Rate",
	[0x02] = "Throughput Performance",
	[0x03] = "Spin-Up Time",
	[0x04] = "Start/Stop Count",
	[0x05] = "Reallocated Sectors Count",
	[0x06] = "Read Channel Margin",
	[0x07] = "Seek Error Rate",
	[0x08] = "Seek Time Performance",
	[0x09] = "Power-On Hours",
	[0x0a] = "Spin Retry Count",
	[0x0b] = "Recalibration Retries",
	[0x0c] = "Power Cycle Count",
	[0x0d] = "Soft Read Error Rate",
	[0x16] = "Current Helium Level",
	[0x17] = "Helium Condition Lower",
	[0x18] = "Helium Condition Upper",
	[0xaa] = "Available Reserved Space",
	[0xab] = "SSD Program Fail Count",
	[0xac] = "SSD Erase Fail Count",
	[0xad] = "SSD Wear Leveling Count",
	[0xae] = "Unexpected Power Loss Count",
	[0xaf] = "Power Loss Protection Failure",
	[0xb0] = "Erase Fail Count",
	[0xb1] = "Wear Range Delta",
	[0xb2] = "Used Reserved Block Count",
	[0xb3] = "Used Reserved Block Count Total",
	[0xb4] = "Unused Reserved Block Count Total",
	[0xb5] = "Program Fail Count Total",
	[0xb6] = "Erase Fail Count",
	[0xb7] = "SATA Downshift Error Count",
	[0xb8] = "End-to-End error",
	[0xb9] = "Head Stability",
	[0xba] = "Induced Op-Vibration Detection",
	[0xbb] = "Reported Uncorrectable Errors",
	[0xbc] = "Command Timeout",
	[0xbd] = "High Fly Writes",
	[0xbe] = "Temperature Difference",
	[0xbf] = "G-sense Error Rate",
	[0xc0] = "Power-off Retract Count",
	[0xc1] = "Load Cycle Count",
	[0xc2] = "Temperature",
	[0xc3] = "Hardware ECC Recovered",
	[0xc4] = "Reallocation Event Count",
	[0xc5] = "Current Pending Sector Count",
	[0xc6] = "(Offline) Uncorrectable Sector Count",
	[0xc7] = "UltraDMA CRC Error Count",
	[0xc8] = "Multi-Zone Error Rate",
	[0xc9] = "Soft Read Error Rate",
	[0xca] = "Data Address Mark errors",
	[0xcb] = "Run Out Cancel",
	[0xcc] = "Soft ECC Correction",
	[0xcd] = "Thermal Asperity Rate",
	[0xce] = "Flying Height",
	[0xcf] = "Spin High Current",
	[0xd0] = "Spin Buzz",
	[0xd1] = "Offline Seek Performance",
	[0xd2] = "Vibration During Write",
	[0xd3] = "Vibration During Write",
	[0xd4] = "Shock During Write",
	[0xdc] = "Disk Shift",
	[0xdd] = "G-Sense Error Rate",
	[0xde] = "Loaded Hours",
	[0xdf] = "Load/Unload Retry Count",
	[0xe0] = "Load Friction",
	[0xe1] = "Load/Unload Cycle Count",
	[0xe2] = "Load 'In'-time",
	[0xe3] = "Torque Amplification Count",
	[0xe4] = "Power-Off Retract Cycle",
	[0xe6] = "GMR Head Amplitude",
	[0xe7] = "Life Left / Temperature",
	[0xe8] = "Endurance Remaining",
	[0xe9] = "Media Wearout Indicator",
	[0xea] = "Average erase count",
	[0xeb] = "Good Block Count",
	[0xf0] = "Head Flying Hours",
	[0xf1] = "Total LBAs Written",
	[0xf2] = "Total LBAs Read",
	[0xf3] = "Total LBAs Written Expanded",
	[0xf4] = "Total LBAs Read Expanded",
	[0xf9] = "NAND Writes (1GiB)",
	[0xfa] = "Read Error Retry Rate",
	[0xfb] = "Minimum Spares Remaining",
	[0xfc] = "Newly Added Bad Flash Block",
	[0xfe] = "Free Fall Protection",
};

/*
 *  S.M.A.R.T command block
 */
static uint8_t cdb[] = {
	CBD_OPERATION_CODE,
	CBD_PROTOCOL_DMA << 1,
	((CBD_T_LENGTH << 0) |
	 (CBD_BYT_BLOK << 1) |
	 (CBD_T_DIR << 3) |
	 (CBD_CK_COND << 5) |
	 (CBD_OFF_LINE << 6)),
	CBD_FEATURES,
	CBD_SECTOR_COUNT,
	CBD_LBA_LOW,
	CBD_LBA_MID,
	CBD_LBA_HIGH,
	CBD_DEVICE,
	CBD_COMMAND,
	CBD_RESVERVED,
	CBD_CONTROL
};

/*
 *  stress_smart_data_free()
 *	free smart data
 */
static void stress_smart_data_free(stress_smart_data_t **data)
{
	if (*data) {
		(*data)->count = 0;
		(*data)->size = 0;
		free(*data);
		*data = NULL;
	}
}

/*
 *  stress_smart_data_read()
 *	read smart data from a device
 */
static stress_smart_data_t *stress_smart_data_read(const char *path)
{
	int fd;
	uint8_t buf[BUF_SZ], sbuf[SENSE_BUF_SZ];
	sg_io_hdr_t sg_io_hdr;
	const stress_smart_raw_value_t *rv_start = (const stress_smart_raw_value_t *)(buf + 2);
	const stress_smart_raw_value_t *rv_end = (const stress_smart_raw_value_t *)(buf + sizeof(buf));
	const stress_smart_raw_value_t *rv;
	stress_smart_data_t *data;
	size_t i, size, values_size;

	if (UNLIKELY(!path))
		return NULL;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return NULL;

	(void)shim_memset(&sg_io_hdr, 0, sizeof(sg_io_hdr));
	sg_io_hdr.interface_id = 'S';
	sg_io_hdr.cmd_len = sizeof(cdb);
	sg_io_hdr.mx_sb_len = sizeof(sbuf);
	sg_io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	sg_io_hdr.dxfer_len = sizeof(buf);
	sg_io_hdr.dxferp = buf;
	sg_io_hdr.cmdp = cdb;
	sg_io_hdr.sbp = sbuf;
	sg_io_hdr.timeout = 35000;
	(void)shim_memset(buf, 0, sizeof(buf));

	if (ioctl(fd, SG_IO, &sg_io_hdr) < 0) {
		(void)close(fd);
		return NULL;
	}
	(void)close(fd);

	for (i = 0, rv = rv_start; (rv < rv_end) && (rv->attr_id); rv++, i++)
		;

	values_size = i * sizeof(stress_smart_raw_value_t);
	size = sizeof(stress_smart_data_t) + values_size;
	data = (stress_smart_data_t *)malloc(size);
	if (!data)
		return NULL;

	(void)shim_memcpy(data->values, rv_start, values_size);
	data->size = size;
	data->count = i;
	return data;
}

/*
 *  stress_smart_data_diff_count()
 *	count smart data changes between begin and end runs
 */
static size_t stress_smart_data_diff_count(stress_smart_dev_t *dev)
{
	size_t i, n;
	stress_smart_data_t *begin, *end;

	begin = dev->data_begin;
	end = dev->data_end;

	if (!begin || !begin->count)
		return 0;
	if (!end || !end->count)
		return 0;

	for (n = 0, i = 0; i < begin->count; i++) {
		const stress_smart_raw_value_t *rv1 = &begin->values[i];
		const uint8_t attr_id = rv1->attr_id;
		size_t j;

		for (j = 0; j < end->count; j++) {
			const stress_smart_raw_value_t *rv2 = &end->values[j];

			if (attr_id == rv2->attr_id) {
				if (rv2->data - rv1->data)
					n++;	/* a value changed, count it */
				break;
			}
		}
	}
	return n;
}

/*
 *  stress_smart_data_diff()
 *	print device and smart attributes that changed
 */
static void stress_smart_data_diff(stress_smart_dev_t *dev)
{
	size_t i;
	stress_smart_data_t *begin, *end;

	begin = dev->data_begin;
	end = dev->data_end;

	if (!begin || !end)
		return;
	if (!begin->count || !end->count)
		return;

	for (i = 0; i < begin->count; i++) {
		const stress_smart_raw_value_t *rv1 = &begin->values[i];
		const uint8_t attr_id = rv1->attr_id;
		size_t j;

		for (j = 0; j < end->count; j++) {
			const stress_smart_raw_value_t *rv2 = &end->values[j];

			if (attr_id == rv2->attr_id) {
				int32_t delta = (int32_t)(rv2->data - rv1->data);

				if (delta) {
					const char *dev_name = (strncmp(dev->dev_name, "/dev/", 5) == 0) ?
								dev->dev_name + 5 : dev->dev_name;
					pr_inf("%-10.10s %2.2x %-30.30s %11" PRIu32 " %11" PRId32 "\n",
						dev_name, attr_id,
						id_str[attr_id] ? id_str[attr_id] : "?",
						rv2->data, delta);
				}
				break;
			}
		}
	}
}

/*
 *  stress_smart_dev_filter()
 * 	discard entries that don't look like device names
 */
static int CONST stress_smart_dev_filter(const struct dirent *d)
{
	size_t len;

	if ((d->d_name[0] == '\0') || (d->d_name[0] == '.'))
		return 0;
	len = strlen(d->d_name);
	if (len < 1)		/* Also unlikely */
		return 0;
	if (isdigit((unsigned char)d->d_name[len - 1]))
		return 0;

	return 1;
}

/*
 *  stress_smart_dev_sort()
 *	sort on dirent filenames
 */
static int CONST stress_smart_dev_sort(const struct dirent **d1, const struct dirent **d2)
{
	int cmp;

	cmp = strcmp((*d1)->d_name, (*d2)->d_name);
	if (cmp < 0)
		return -1;
	if (cmp > 1)
		return 1;
	return 0;
}

/*
 *  stress_smart_read_devs()
 *	scan across block devices and populate a linked list
 *	of all devices that can supply S.M.A.R.T. data
 */
static void stress_smart_read_devs(void)
{
	struct dirent **devs = NULL;
	int i, n;

	smart_devs.dev = NULL;

	n = scandir("/dev", &devs, stress_smart_dev_filter, stress_smart_dev_sort);
	if (n < 1)
		return;

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];
		struct stat buf;
		int ret;
		const struct dirent *d = devs[i];

		(void)snprintf(path, sizeof(path), "/dev/%s", d->d_name);
		ret = shim_stat(path, &buf);
		if ((ret == 0) && (S_ISBLK(buf.st_mode))) {
			stress_smart_dev_t *dev;
			stress_smart_data_t *data;

			data = stress_smart_data_read(path);
			if (data) {
				/* Allocate, silently ignore alloc failure */
				dev = (stress_smart_dev_t *)calloc(1, sizeof(*dev));
				if (dev) {
					dev->dev_name = shim_strdup(path);
					dev->data_begin = data;
					dev->data_end = NULL;
					dev->next = smart_devs.dev;
					smart_devs.dev = dev;
				} else {
					stress_smart_data_free(&data);
				}
			}
		}
		free(devs[i]);
	}
	free(devs);
}

/*
 *  stress_smart_free_devs()
 *	free list of smart enabled device information
 */
static void stress_smart_free_devs(void)
{
	stress_smart_dev_t *dev = smart_devs.dev;

	while (dev) {
		stress_smart_dev_t *next = dev->next;

		free(dev->dev_name);
		stress_smart_data_free(&dev->data_begin);
		stress_smart_data_free(&dev->data_end);
		free(dev);

		dev = next;
	}
	smart_devs.dev = NULL;
}
#endif

/*
 *  stress_smart_start()
 *	fetch beginning smart data
 */
void stress_smart_start(void)
{
	if (g_opt_flags & OPT_FLAGS_SMART) {
#if defined(HAVE_SMART)
		stress_smart_read_devs();
#else
		pr_inf("note: --smart option not available for this system\n");
#endif
	}
}

/*
 *  stress_smart_stop()
 *	fetch stop smart data and print any changes
 */
void stress_smart_stop(void)
{
	if (g_opt_flags & OPT_FLAGS_SMART) {
#if defined(HAVE_SMART)
		stress_smart_dev_t *dev;
		size_t deltas = 0;
		size_t devs = 0;

		for (dev = smart_devs.dev; dev; dev = dev->next) {
			dev->data_end = stress_smart_data_read(dev->dev_name);
			deltas += stress_smart_data_diff_count(dev);
			devs++;
		}

		if (deltas) {
			pr_inf("%-10.10s %2.2s %-30.30s %11.11s %11.11s\n",
				"Device", "ID", "S.M.A.R.T. Attribute", "Value", "Change");
			for (dev = smart_devs.dev; dev; dev = dev->next) {
				stress_smart_data_diff(dev);
			}
		} else {
			if (devs == 0) {
				char *extra;

				if (stress_check_capability(SHIM_CAP_IS_ROOT)) {
					extra = "";
				} else {
					extra = " (try running as root)";
				}
				pr_inf("could not find any S.M.A.R.T. enabled devices%s\n", extra);
			} else {
				pr_inf("no S.M.A.R.T. data statistics changed on %zd device%s\n",
					devs, devs > 1 ? "s" : "");
			}
		}
		stress_smart_free_devs();
#else
		pr_inf("S.M.A.R.T. functionality not available\n");
#endif
	}
}
