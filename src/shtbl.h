/* $Id: shtbl.h,v 1.2 2009/08/21 03:09:36 strauman Exp $ */
#ifndef SIMPLE_HASHTAB_H
#define SIMPLE_HASHTAB_H

/* Simple hash table with open addressing (linear probing).
 * We use Knuth's 'golden-ratio' hash function.
 *
 * Author: Till Straumann <strauman@slac.stanford.edu>, 2009.
 */


#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct SHTblRec_ *SHTbl;
typedef uint32_t         SHTblKey;
typedef void             *SHTblEntry;

/* Create an empty hash table with n_bucket entries;
 * 'n_bucket' is up-aligned to the next power of two.
 *
 * The storage layout of entry records is defined
 * by the application but the key can be found
 * at 'key_off' bytes from the start of an
 * entry record (and it must be properly aligned).
 */
SHTbl
shtblCreate(unsigned n_bucket, unsigned long key_off);

/*
 * Destroy a hash table (but individual entries
 * are untouched.)
 *
 * If non-NULL, the 'cleanup' routine is executed
 * on every non-empty bucket.
 */
void
shtblDestroy(SHTbl shtbl, void (*cleanup)(SHTblEntry, void *closure), void *closure);

/*
 * Locate key/entry
 */
SHTblEntry
shtblFind(SHTbl shtbl, SHTblKey key);


/* Error codes */
#define SHTBL_FULL	         (-1)	/* table is completely full */
#define SHTBL_KEY_EXISTS     (-2)   /* entry with 'key' already exists (shtblAdd()) */
#define SHTBL_KEY_NOTFND     (-3)   /* entry with 'key' does not exist (shtblDel()) */
/*
 * Add new entry;
 *
 * RETURNS: zero on success, nonzero on error.
 */
int
shtblAdd(SHTbl shtbl, SHTblEntry entry);

/*
 * Add new entry; if 'key' already exists then the
 * new entry replaces the existing one.
 *
 * RETURNS: zero on success, nonzero on error.
 *          If successful, the previously existing
 *          entry for 'key' is returned in '*entry'.
 *          If no entry for 'key' existed '*entry'
 *          is set to NULL.
 *
 *          If the 'add_fail' argument is nonzero
 *          then the call fails if no existing entry
 *          can be found.
 */

#define SHTBL_ADD_FAIL 1
int
shtblRpl(SHTbl shtbl, SHTblEntry *entry, int add_fail);

/*
 * Remove an entry;
 *
 * RETURNS: zero on success, nonzero on error.
 */
int
shtblDel(SHTbl shtbl, SHTblEntry entry);

/*
 * Obtain size and currently used number of slots.
 */
void
shtblStats(SHTbl shtbl, unsigned *p_size, unsigned *p_used);

#ifdef __cplusplus
}
#endif

#endif
