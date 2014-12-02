#ifndef _CVSTYPES_H_
#define _CVSTYPES_H_

/*
 * Typedefs have been carefully chosen to minimize working set.
 */

/*
 * Use instead of bool in frequently used structures to reduce
 * working-set size.
 */
typedef char flag;

/*
 * On 64-bit Linux a time_t is 8 bytes.  We want to reduce memory
 * footprint; by storing dates as 32-bit offsets from the beginning of
 * 1982 (the year RCS was released) we can cover dates all the way to
 * 2118-02-07T06:28:15 in half that size.  If you're still doing
 * conversions after that you'll just have to change this to a uint64_t. 
 * Yes, the code *does* sanity-check for input dates older than this epoch.
 */
typedef uint32_t	cvstime_t;
#define RCS_EPOCH	378691200	/* 1982-01-01T00:00:00 */
#define RCS_OMEGA	UINT32_MAX	/* 2118-02-07T06:28:15 */

/*
 * This type must be wide enough to enumerate every CVS revision.
 * There's a sanity check in the code.
 */
typedef uint32_t	serial_t;
#define MAX_SERIAL_T	UINT32_MAX

/*
 * This type must be wide enough to count all branches cointaining a commit.
 * There's a sanity check in the code.
 */
typedef uint8_t			branchcount_t;
#define MAX_BRANCHCOUNT_T	UINT8_MAX

/* Hash values */
typedef uint32_t        hash_t;

#endif /* _CVSTYPES_H_ */
