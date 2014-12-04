#ifndef _HASH_H_
#define _HASH_H_

#include "cvstypes.h"

hash_t
hash_init(void);

hash_t
hash_string(const char *val);

hash_t
hash_value(const char *val, size_t len);

hash_t
hash_mix(hash_t seed, const char *val, size_t len);

hash_t
hash_mix_string(hash_t seed, const char *val);


#define HASH_INIT(hash) hash_t hash = hash_init()
#define HASH_MIX_SEED(hash, seed, val) hash = hash_mix((seed), (const char *)&(val), sizeof(val))
#define HASH_VALUE(val) hash_value((const char *)&(val), sizeof(val))
#define HASH_MIX(hash, val) hash = hash_mix((hash), (const char *)&(val), sizeof(val))
#define HASH_COMBINE(h1, h2) ((h1) ^ (h2))

#endif /* _HASH_H_ */
