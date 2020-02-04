/*
 * Copyright (C) 2014-2020 Canonical, Ltd.
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

/*
 *  stress_hash_jenkin()
 *	Jenkin's hash on random data
 *	http://www.burtleburtle.net/bob/hash/doobs.html
 */
uint32_t HOT OPTIMIZE3 stress_hash_jenkin(const uint8_t *data, const size_t len)
{
	register size_t i;
	register uint32_t h = 0;

	for (i = 0; i < len; i++) {
		h += *data++;
		h += h << 10;
		h ^= h >> 6;
	}
	h += h << 3;
	h ^= h >> 11;
	h += h << 15;

	return h;
}

/*
 *  stress_hash_pjw()
 *	Hash a string, from Aho, Sethi, Ullman, Compiling Techniques.
 */
uint32_t HOT OPTIMIZE3 stress_hash_pjw(const char *str)
{
	register uint32_t h = 0;

	while (*str) {
		register uint32_t g;

		h = (h << 4) + (*str);
		if (0 != (g = h & 0xf0000000)) {
			h = h ^ (g >> 24);
			h = h ^ g;
		}
		str++;
	}
	return h;
}

/*
 *  stress_hash_djb2a()
 *	Hash a string, from Dan Bernstein comp.lang.c (xor version)
 */
uint32_t HOT OPTIMIZE3 stress_hash_djb2a(const char *str)
{
	register uint32_t hash = 5381;
	register int c;

	while ((c = *str++)) {
		/* (hash * 33) ^ c */
		hash = ((hash << 5) + hash) ^ c;
	}
	return hash;
}

/*
 *  stress_hash_fnv1a()
 *	Hash a string, using the improved 32 bit FNV-1a hash
 */
uint32_t HOT OPTIMIZE3 stress_hash_fnv1a(const char *str)
{
	register uint32_t hash = 5381;
	const uint32_t fnv_prime = 16777619; /* 2^24 + 2^9 + 0x93 */
	register int c;

	while ((c = *str++)) {
		hash ^= c;
		hash *= fnv_prime;
	}
	return hash;
}

/*
 *  stress_hash_sdbm()
 *	Hash a string, using the sdbm data base hash and also
 *	apparently used in GNU awk.
 */
uint32_t HOT OPTIMIZE3 stress_hash_sdbm(const char *str)
{
	register uint32_t hash = 0;
	register int c;

	while ((c = *str++))
		hash = c + (hash << 6) + (hash << 16) - hash;
	return hash;
}

/*
 *  stress_hash_create()
 *	create a hash table with size of n base hash entries
 */
stress_hash_table_t *stress_hash_create(const size_t n)
{
	stress_hash_table_t *hash_table;

	if (n == 0)
		return NULL;

	hash_table = calloc(1, sizeof(*hash_table));
	if (!hash_table)
		return NULL;

	hash_table->table = calloc(n, sizeof(stress_hash_t *));
	if (!hash_table->table) {
		free(hash_table);
		return NULL;
	}
	hash_table->n = n;

	return hash_table;
}

#define HASH_STR(hash)	(((char *)hash) + sizeof(*hash))

static inline stress_hash_t *stress_hash_find(stress_hash_t *hash, const char *str)
{
	while (hash) {
		if (!strcmp(str, HASH_STR(hash)))
			return hash;
		hash = hash->next;
	}
	return NULL;
}

/*
 *  stress_hash_get()
 *	get a hash entry based on the given string. returns NULL if it does
 *	not exist
 */
stress_hash_t *stress_hash_get(stress_hash_table_t *hash_table, const char *str)
{
	uint32_t h;

	if (UNLIKELY(!hash_table))
		return NULL;
	if (UNLIKELY(!str))
		return NULL;

	h = stress_hash_sdbm(str) % hash_table->n;
	return stress_hash_find(hash_table->table[h], str);
}

/*
 *  stress_hash_add()
 *	add a hash entry based on the given string. If the string already is
 *	hashed it is not re-added.  Returns null if an error occurs (e.g. out of
 *	memory).
 */
stress_hash_t *stress_hash_add(stress_hash_table_t *hash_table, const char *str)
{
	stress_hash_t *hash;
	uint32_t h;
	size_t len;

	if (UNLIKELY(!hash_table))
		return NULL;
	if (UNLIKELY(!str))
		return NULL;

	h = stress_hash_sdbm(str) % hash_table->n;
	hash = stress_hash_find(hash_table->table[h], str);
	if (hash)
		return hash;

	/* Not found, so add a new hash */
	len = strlen(str) + 1;
	hash = malloc(sizeof(*hash) + len);
	if (UNLIKELY(!hash))
		return NULL;

	hash->next = hash_table->table[h];
	hash_table->table[h] = hash;
	(void)memcpy(HASH_STR(hash), str, len);

	return hash;
}

/*
 *   stress_hash_delete()
 *	delete a hash table and all entries in the table
 */
void stress_hash_delete(stress_hash_table_t *hash_table)
{
	size_t i;

	if (!hash_table)
		return;

	for (i = 0; i < hash_table->n; i++) {
		stress_hash_t *hash = hash_table->table[i];

		while (hash) {
			stress_hash_t *next = hash->next;

			free(hash);
			hash = next;
		}
	}
	free(hash_table->table);
	free(hash_table);
}
