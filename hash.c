#include <stddef.h>
#include <sys/types.h>
#include <inttypes.h>
#include <limits.h>
#include "hash.h"

/* FNV Hash Constants from http://isthe.com/chongo/tech/comp/fnv/ */

//#if UINT_MAX == UINT32_MAX
#define HASH_FNV_INITIAL 2166136261U
#define HASH_FNV_MIXVAL 16777619U
/*
#elif UINT_MAX == UINT64_MAX
#define HASH_FNV_INITIAL 14695981039346656037UL
#define HASH_FNV_MIXVAL  1099511628211UL
#endif
*/
#define HASH_MIX_FNV1A(hash, val) hash = (hash ^ (uint8_t)(val)) * HASH_FNV_MIXVAL

static hash_t
fnv1a_hash_init(void)
{
    return HASH_FNV_INITIAL;
}

static hash_t
fnv1a_hash_mix_string(hash_t seed, const char *val) 
{
    uint8_t c;
    while ((c = (uint8_t)*val++))
	HASH_MIX_FNV1A(seed, c);
    return seed;
}

static hash_t
fnv1a_hash_string(const char *val)
{
    return fnv1a_hash_mix_string(HASH_FNV_INITIAL, val);
}

static hash_t
fnv1a_hash_mix(hash_t seed, const char *val, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++)
	HASH_MIX_FNV1A(seed, val[i]);
    return seed;
}

static hash_t
fnv1a_hash_value(const char *val, size_t len)
{
    return fnv1a_hash_mix(HASH_FNV_INITIAL, val, len);
}


static hash_t crc32_table[256];

static void
generate_crc32_table(void)
{
    hash_t	c, p;
    int		n, m;

    p = 0xedb88320;
    for (n = 0; n < 256; n++) {
	c = n;
	for (m = 0; m < 8; m++)
	    c = (c >> 1) ^ ((c & 1) ? p : 0);
	crc32_table[n] = c;
    }
}

static hash_t
crc32(const char *string)
{
    hash_t		crc32 = ~0;
    unsigned char	c;

    if (crc32_table[1] == 0) generate_crc32_table();
    while ((c = (unsigned char) *string++))
	crc32 = (crc32 >> 8) ^ crc32_table[(crc32 ^ c) & 0xff];
    return ~crc32;
}

hash_t
hash_init(void)
{
    return fnv1a_hash_init();
}

hash_t
hash_string(const char *val)
{
    return fnv1a_hash_string(val);
}

hash_t
hash_mix(hash_t seed, const char *val, size_t len)
{
    return fnv1a_hash_mix(seed, val, len);
}

hash_t
hash_value(const char *val, size_t len)
{
    return fnv1a_hash_value(val, len);
}

hash_t
hash_mix_string(hash_t seed, const char *val)
{
    return fnv1a_hash_mix_string(seed, val);
}

//end

