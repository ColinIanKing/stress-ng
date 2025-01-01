/*
 * Copyright (C) 2022-2025 Colin Ian King
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
#include <netinet/sctp.h>

int main(void)
{
	/* Minimum expected scheduler types expected for sctp stressor */
	enum sctp_sched_type types[] = {
		SCTP_SS_FCFS,
		SCTP_SS_PRIO,
		SCTP_SS_RR,
	};
	return (int)sizeof(types) / sizeof(types[0]);
}
