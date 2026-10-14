/*
 * Copyright (C) 2026      Gianmarco De Gregori
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
#if !defined(__linux__)
#error requires linux to build
#endif

#include <linux/ovpn.h>

/*
 *  The presence of linux/ovpn.h says nothing about what it contains: the
 *  header travelled through several out-of-tree revisions before ovpn was
 *  merged in 6.16, and a system can easily have an older or partial copy of
 *  it. Reference the generic netlink commands and attributes the stressor
 *  actually uses, so a header that predates them is not included and the
 *  stressor falls back to its own copy of the uapi, stress-ovpn-uapi.h.
 */
int main(void)
{
	static const char *family = OVPN_FAMILY_NAME;
	const enum ovpn_cipher_alg ciphers[] = {
		OVPN_CIPHER_ALG_NONE, OVPN_CIPHER_ALG_AES_GCM,
		OVPN_CIPHER_ALG_CHACHA20_POLY1305,
	};
	const enum ovpn_key_slot slots[] = {
		OVPN_KEY_SLOT_PRIMARY, OVPN_KEY_SLOT_SECONDARY,
	};
	const int cmds[] = {
		OVPN_CMD_PEER_NEW, OVPN_CMD_PEER_SET, OVPN_CMD_PEER_GET,
		OVPN_CMD_PEER_DEL, OVPN_CMD_KEY_NEW, OVPN_CMD_KEY_GET,
		OVPN_CMD_KEY_SWAP, OVPN_CMD_KEY_DEL,
	};
	const int attrs[] = {
		OVPN_A_IFINDEX, OVPN_A_PEER, OVPN_A_KEYCONF,
		OVPN_A_PEER_ID, OVPN_A_PEER_SOCKET,
		OVPN_A_PEER_REMOTE_IPV4, OVPN_A_PEER_REMOTE_IPV6,
		OVPN_A_PEER_REMOTE_IPV6_SCOPE_ID,
		OVPN_A_PEER_REMOTE_PORT, OVPN_A_PEER_VPN_IPV4,
		OVPN_A_PEER_VPN_IPV6, OVPN_A_PEER_KEEPALIVE_INTERVAL,
		OVPN_A_PEER_KEEPALIVE_TIMEOUT,
		OVPN_A_PEER_VPN_RX_BYTES, OVPN_A_PEER_VPN_TX_BYTES,
		OVPN_A_KEYCONF_PEER_ID, OVPN_A_KEYCONF_SLOT,
		OVPN_A_KEYCONF_KEY_ID, OVPN_A_KEYCONF_CIPHER_ALG,
		OVPN_A_KEYCONF_ENCRYPT_DIR, OVPN_A_KEYCONF_DECRYPT_DIR,
		OVPN_A_KEYDIR_CIPHER_KEY, OVPN_A_KEYDIR_NONCE_TAIL,
	};

	(void)family;
	(void)ciphers;
	(void)slots;
	(void)cmds;
	(void)attrs;

	return OVPN_A_MAX + OVPN_A_PEER_MAX + OVPN_A_KEYCONF_MAX;
}
