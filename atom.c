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
#include "hash.h"
#include <stdint.h>
#ifdef THREADS
#include <pthread.h>
#endif /* THREADS */
/*****************************************************************************

From http://planetmath.org/goodhashtableprimes:

In the course of designing a good hashing configuration, it is helpful
to have a list of prime numbers for the hash table size.

The following is such a list. It has the properties that:

    1. each number in the list is prime
    2. each number is slightly less than twice the size of the previous
    3. each number is as far as possible from the nearest two powers of two

Using primes for hash tables is a good idea because it minimizes
clustering in the hashed table. Item (2) is nice because it is
convenient for growing a hash table in the face of expanding
data. Item (3) has, allegedly, been shown to yield especially good
results in practice.

And here is the list:

lwr 	upr 	% err   	prime
2^5 	2^6 	10.416667 	53
2^6 	2^7 	1.041667 	97
2^7 	2^8 	0.520833 	193
2^8 	2^9 	1.302083 	389
2^9 	2^10 	0.130208 	769
2^10 	2^11 	0.455729 	1543
2^11 	2^12 	0.227865 	3079
2^12 	2^13 	0.113932 	6151
2^13 	2^14 	0.008138 	12289
2^14 	2^15 	0.069173 	24593
2^15 	2^16 	0.010173 	49157
2^16 	2^17 	0.013224 	98317
2^17 	2^18 	0.002543 	196613
2^18 	2^19 	0.006358 	393241
2^19 	2^20 	0.000127 	786433
2^20 	2^21 	0.000318 	1572869
2^21 	2^22 	0.000350 	3145739
2^22 	2^23 	0.000207 	6291469
2^23 	2^24 	0.000040 	12582917
2^24 	2^25 	0.000075 	25165843
2^25 	2^26 	0.000010 	50331653
2^26 	2^27 	0.000023 	100663319
2^27 	2^28 	0.000009 	201326611
2^28 	2^29 	0.000001 	402653189
2^29 	2^30 	0.000011 	805306457
2^30 	2^31 	0.000000 	1610612741

The columns are, in order, the lower bounding power of two, the upper
bounding power of two, the relative deviation (in percent) of the
prime number from the optimal middle of the first two, and finally the
prime itself.

Happy hashing!

*****************************************************************************/

/*
 * This prime number is scaled to be effective for the NetBSD src
 * repository, which at around 135K masters is the largest we know of.
 * The intent is to reduce expected depth of the hash buckets in the
 * worst case to about 4.  Space cost on a 64-bit machine is 8 times
 * this in bytes.
 */
#define HASH_SIZE	49157

unsigned int natoms;	/* we report this so we can tune the hash properly */

typedef struct _hash_bucket {
    struct _hash_bucket	*next;
    hash_t		hash;
    char		string[0];
} hash_bucket_t;

static hash_bucket_t	*buckets[HASH_SIZE];
#ifdef THREADS
static pthread_mutex_t bucket_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif /* THREADS */

const char *
atom(const char *string)
/* intern a string, avoiding having separate storage for duplicate copies */
{
    hash_t		hash = hash_string(string);
    hash_bucket_t	**head = &buckets[hash % HASH_SIZE];
    hash_bucket_t	*b;
    int			len;

    while ((b = *head)) {
collision:
	if (b->hash == hash && !strcmp(string, b->string))
	    return b->string;
	head = &(b->next);
    }
#ifdef THREADS
    if (threads > 1)
	pthread_mutex_lock(&bucket_mutex);
#endif /* THREADS */
    if ((b = *head)) {
#ifdef THREADS
	if (threads > 1)
	    pthread_mutex_unlock(&bucket_mutex);
#endif /* THREADS */
	goto collision;
    }

    len = strlen(string);
    b = xmalloc(sizeof(hash_bucket_t) + len + 1, __func__);
    b->next = 0;
    b->hash = hash;
    memcpy(b->string, string, len + 1);
    *head = b;
    natoms++;
#ifdef THREADS
    if (threads > 1)
	pthread_mutex_unlock(&bucket_mutex);
#endif /* THREADS */
    return b->string;
}

typedef struct _number_bucket {
    struct _number_bucket *next;
    cvs_number number;
} number_bucket_t;

#define NUMBER_HASH_SIZE 6151

static number_bucket_t  *number_buckets[NUMBER_HASH_SIZE];
#ifdef THREADS
static pthread_mutex_t number_bucket_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif /* THREADS */

/*
 * Intern a revision number
 * netbsd-pkgsrc calls this 42,000,000 times for 22,000 distinct values
 */
const cvs_number *
atom_cvs_number(const cvs_number n)
{
    size_t          bucket = hash_cvs_number(&n) % NUMBER_HASH_SIZE;
    number_bucket_t **head = &number_buckets[bucket];
    number_bucket_t *b;

    while ((b = *head)) {
    collision:
	if (cvs_number_equal(&b->number, &n))
	    return &b->number;
	head = &(b->next);
    }
#ifdef THREADS
    if (threads > 1)
	pthread_mutex_lock(&number_bucket_mutex);
#endif /* THREADS */
    if ((b = *head)) {
#ifdef THREADS
	if (threads > 1)
	    pthread_mutex_unlock(&number_bucket_mutex);
#endif /* THREADS */
	goto collision;
    }

    b = xmalloc(sizeof(number_bucket_t), __func__);
    b->next = NULL;
    memcpy(&b->number, &n, sizeof(cvs_number));
    *head = b;
#ifdef THREADS
    if (threads > 1)
	pthread_mutex_unlock(&number_bucket_mutex);
#endif /* THREADS */
    return &b->number;
}
void
discard_atoms(void)
/* empty all string buckets */
{
    hash_bucket_t	**head, *b;
    int			i;

#ifdef THREADS
    if (threads > 1)
	pthread_mutex_lock(&bucket_mutex);
#endif /* THREADS */
    for (i = 0; i < HASH_SIZE; i++)
	for (head = &buckets[i]; (b = *head);) {
	    *head = b->next;
	    free(b);
	}
#ifdef THREADS
    if (threads > 1) {
	pthread_mutex_unlock(&bucket_mutex);
	/*
	 * This is irreversible, and will have to be factored out if
	 * dicard_atoms() is ever called anywhere but in final cleanup.
	 */
	pthread_mutex_destroy(&bucket_mutex);
    }
#endif /* THREADS */
}

/* end */
