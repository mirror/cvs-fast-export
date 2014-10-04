/*
 *  Copyright © 2006 Keith Packard <keithp@keithp.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */

#include "cvs.h"
#include <stdint.h>
#include <pthread.h>

typedef uint32_t	crc32_t;

static crc32_t crc32_table[256];

static void
generate_crc32_table(void)
{
    crc32_t	c, p;
    int		n, m;

    p = 0xedb88320;
    for (n = 0; n < 256; n++)
    {
	c = n;
	for (m = 0; m < 8; m++)
	    c = (c >> 1) ^ ((c & 1) ? p : 0);
	crc32_table[n] = c;
    }
}

static crc32_t
crc32 (char *string)
{
    crc32_t		crc32 = ~0;
    unsigned char	c;

    if (crc32_table[1] == 0) generate_crc32_table();
    while ((c = (unsigned char) *string++))
	crc32 = (crc32 >> 8) ^ crc32_table[(crc32 ^ c) & 0xff];
    return ~crc32;
}

#define HASH_SIZE	9013	/* prime for better hash performance */

typedef struct _hash_bucket {
    struct _hash_bucket	*next;
    crc32_t		crc;
    bloom_t        	bloom;
    char		string[0];
} hash_bucket_t;

static hash_bucket_t	*buckets[HASH_SIZE];
static pthread_mutex_t bucket_mutex = PTHREAD_MUTEX_INITIALIZER;

#define offsetof(T, f)  (size_t)(&((T *)0)->f)
#define containerof(fp, T, f) (T *)((char *)(fp) - offsetof(T, f))

const bloom_t *
atom_bloom(char *atom)
{
    hash_bucket_t *b = containerof(atom, hash_bucket_t, string);
    return &b->bloom;
}

#define BLOOM_K 9 /* TODO optimal? */

static void
make_bloom(crc32_t crc, bloom_t *b)
{
    unsigned k, bit;
    bloomword n = crc;

    memset(b, 0, sizeof *b);
    for (k = 0; k < BLOOM_K; k++) {
        n ^= n >> 12;
        n ^= n << 25;
        n ^= n >> 27;
	n *=  (bloomword)2685821657736338717;

	bit = n % BLOOMSIZE;
	b->el[bit / BLOOMWIDTH] |= (bloomword)1 << (bit % BLOOMWIDTH);
    }
}


char *
atom(char *string)
/* intern a string, avoiding having separate storage for duplicate copies */
{
    crc32_t		crc = crc32 (string);
    hash_bucket_t	**head = &buckets[crc % HASH_SIZE];
    hash_bucket_t	*b;
    int			len = strlen(string);

    while ((b = *head)) {
collision:
	if (b->crc == crc && !strcmp(string, b->string))
	    return b->string;
	head = &(b->next);
    }
    pthread_mutex_lock(&bucket_mutex);
    if ((b = *head)) {
	pthread_mutex_unlock(&bucket_mutex);
	goto collision;
    }

    b = xmalloc(sizeof(hash_bucket_t) + len + 1, __func__);
    b->next = 0;
    b->crc = crc;
    make_bloom(crc, &b->bloom);
    memcpy(b->string, string, len + 1);
    *head = b;
    pthread_mutex_unlock(&bucket_mutex);
    return b->string;
}

void
discard_atoms(void)
/* empty all string buckets */
{
    hash_bucket_t	**head, *b;
    int			i;

    pthread_mutex_lock(&bucket_mutex);
    for (i = 0; i < HASH_SIZE; i++)
	for (head = &buckets[i]; (b = *head);) {
	    *head = b->next;
	    free(b);
	}
    pthread_mutex_unlock(&bucket_mutex);
}

/* end */
