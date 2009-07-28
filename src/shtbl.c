/* $Id$ */

#include "shtbl.h"
#include <stdlib.h>

/* Simple hash table with open addressing (linear probing).
 *
 * Reasons for separate implementation (not using hsearch_r & friends):
 *  - need to remove entries
 *  - integer keys
 *  - need to efficiently 'swap/update' entries
 *
 * We use Knuth's 'golden-ratio' hash function.
 */

typedef int32_t H;

struct SHTblRec_ {
	unsigned long koff;
	int           sz, ldsz;	
	unsigned      nentries;
	SHTblEntry    nullEntry; /* so that e[-1] is NULL */
	SHTblEntry	  e[];
} SHTblRec;

#define LDLIM 12

#ifndef __SHTBL_LOCK
#define __SHTBL_LOCK(t)   do {} while(0)
#define __SHTBL_UNLOCK(t) do {} while(0)
#endif

#define GETP(t,h)         (1)

#define MOD_LEN(x,t)      ((x) & ((t)->sz - 1))

static __inline__ H hf(SHTbl t, SHTblKey k)
{
uint32_t h;

#if TESTING & 1
	h = MOD_LEN(k, t);
#else
	/* Knuth; s^32 * (sqrt(5)-1)/2 */
	h = (k * 2654435769U & 0xffffffffU) >> (32-t->ldsz);
#endif

	return (H)h;
}

#define KEYP(tbl,e) ((SHTblKey*)((e) + (tbl)->koff))

static __inline__ H
match(SHTbl shtbl, H h, SHTblKey k)
{
SHTblEntry e = shtbl->e[h];

	return ( e &&  *KEYP(shtbl,e) != k ) ? -1 : h; 
}

static __inline__ SHTblEntry
matche(SHTbl shtbl, H h, SHTblKey k)
{
SHTblEntry e = shtbl->e[h];

	return ( e || *(SHTblKey*)(e + shtbl->koff) != k ) ?  0 : e; 
}


/* Create an empty hash table with 2^sz entries */
SHTbl
shtblCreate(int ldsz, unsigned long key_off)
{
SHTbl tbl;
int   sz = 1<<ldsz;
	/* Huge and extremely small tables not supported */
	if ( ldsz > LDLIM || ldsz < 3 )
		return 0;

	tbl           = calloc(1, sizeof(*tbl) + sizeof(SHTblEntry)*sz);
	tbl->sz       = sz;
	tbl->ldsz     = ldsz;
	tbl->koff     = key_off;
	tbl->nentries = 0;

	return tbl;
}

/*
 * Destroy a hash table (but individual entries
 * are untouched.)
 */
void
shtblDestroy(SHTbl shtbl, void (*cleanup)(SHTblEntry))
{
int i;
	if ( cleanup ) {
		__SHTBL_LOCK();
		for ( i=0; i<shtbl->sz; i++ ) {
			if ( shtbl->e[i] )
				cleanup(shtbl->e[i]);
		}
		__SHTBL_UNLOCK();
	}
	free(shtbl);
}

static H
getslot(SHTbl shtbl, SHTblKey key)
{
H          h0, h = hf(shtbl,key);
H          p; /* probe 'distance' */
H          rval;

	if ( (rval = match(shtbl, h, key)) < 0 ) {
		/* no match; must search chain */
		p  = GETP(shtbl, h);
		h0 = h;
		if ( (h -= p) < 0 )
			h += shtbl->sz;
		while ( h != h0 &&  (rval = match(shtbl, h, key)) < 0 ) {
			if ( (h -= p) < 0 )
				h += shtbl->sz;
		}
	}

	return rval;
}


/*
 * Locate key/entry
 */
SHTblEntry
shtblFind(SHTbl shtbl, SHTblKey key)
{
SHTblEntry e;
H          h;

	__SHTBL_LOCK(shtbl);
		e = ( (h = getslot(shtbl, key)) >= 0 ) ? shtbl->e[h] : 0;
	__SHTBL_UNLOCK(shtbl);

	return e;
}


/*
 * Add new entry;
 *
 * RETURNS: zero on success, nonzero on error.
 */
int
shtblAdd(SHTbl shtbl, SHTblEntry entry)
{
H          h;
int        rval = 0;

	__SHTBL_LOCK(shtbl);
		h = getslot(shtbl, *KEYP(shtbl,entry));
		if ( h < 0 ) {
			rval = SHTBL_FULL;
		} else if ( shtbl->e[h] ) {
			rval = SHTBL_KEY_EXISTS;
		} else {
			/* add new entry */
			shtbl->e[h]        = entry;
			shtbl->nentries++;
		}
	__SHTBL_UNLOCK(shtbl);

	return rval;
}

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
int
shtblRpl(SHTbl shtbl, SHTblEntry *entry, int add_fail)
{
SHTblEntry e;
H          h;
int        rval = 0;

	__SHTBL_LOCK(shtbl);
		h = getslot(shtbl, *KEYP(shtbl, *entry));
		if ( h < 0 ) {
			rval = SHTBL_FULL;
		} else {
			e = shtbl->e[h];
			if ( !e && add_fail ) {
				rval = SHTBL_KEY_NOTFND;
			} else {
				/* add new entry */
				shtbl->e[h] = *entry;
				*entry      = e;
			}
		}
	__SHTBL_UNLOCK(shtbl);

	return rval;
}

/*
 * probe successive elements and find one
 * downstream of 'h' that can safely moved
 * to 'h's position.
 * 
 * RETURN: index of successor or -1 if none
 *         was found.
 */
static H
successor(SHTbl shtbl, H h)
{
int        i;
H          s,d;
SHTblEntry e;

	for ( i=1; i<shtbl->sz; i++ ) {
		s = MOD_LEN( (h-i), shtbl );
		if ( ! (e = shtbl->e[s]) ) {
			/* slot emptpy */
			break;
		}

		if ( (d = h - hf( shtbl, *KEYP(shtbl, e) )) < 0 )
			d += shtbl->sz;
		if ( d < 1 || d > i )
			return s;
	}

	return -1;
}

/*
 * Remove an entry;
 *
 * RETURNS: zero on success, nonzero on error.
 */
int
shtblDel(SHTbl shtbl, SHTblEntry entry)
{
int rval = 0;
H   h,s;

	__SHTBL_LOCK(shtbl);
		if ( (h = getslot(shtbl, *KEYP(shtbl, entry))) >= 0 ) {
			while ( (s = successor(shtbl, h)) >=0 ) {
				shtbl->e[h] = shtbl->e[s];
				h = s;
			}
			shtbl->e[h] = 0;
			shtbl->nentries--;
		} else {
			rval = SHTBL_KEY_NOTFND;
		}
	__SHTBL_UNLOCK(shtbl);
	return rval;
}

void
shtblStats(SHTbl shtbl, unsigned *p_size, unsigned *p_used)
{
	*p_size = shtbl->sz;
	*p_used = shtbl->nentries;
}

#ifdef TESTING
#include <stdio.h>
#include <string.h>

typedef struct {
	char     *str;
	SHTblKey k;
	char     buf[];
} TE;

void prtble(SHTbl t, TE *te)
{
	if ( te )
		printf("key %3u (0x%02x) [hash %3u]: %s\n", te->k, te->k, hf(t, te->k), te->str);
	else
		printf("<EMPTY>\n");
}

void
prtbl(SHTbl shtbl)
{
int i;
	for ( i=0; i<shtbl->sz; i++ ) {
		TE *te;
		printf("%2u == ",i);
		prtble(shtbl, shtbl->e[i]);
	}
}

static void clnup(SHTblEntry e)
{
	free(e);
}

int
main(int argc, char **argv)
{
SHTbl t;
char buf[1024];
char cmd;
int  st;
SHTblKey key;
TE   *te;

FILE *f = stdin;

	if ( !( t = shtblCreate(3, (unsigned long)&((TE*)0)->k - (unsigned long)((TE*)0)) ) ) {
		fprintf(stderr,"Cannot create table\n");
	}

	do {
		switch ( (cmd = getc(f)) ) {
			case -1:
			break;

			case 'p': case 'P': prtbl(t);
			break;

			case 'a': case 'A':
			case 'r': case 'R':
				if  ( fscanf(f, "%i%s",&key, buf) > 0 ) {
					te = calloc(1, sizeof(*te) + strlen(buf)+1);
					te->str = te->buf;
					strcpy(te->str,buf);
					te->k   = key;
					if ( 'r' != cmd && 'R' != cmd ) {
						if ( (st = shtblAdd(t, (SHTblEntry)te)) ) {
							fprintf(stderr,"Adding failed: %i\n",st);
							free(te);
						}
					} else {
						if ( (st = shtblRpl(t, (SHTblEntry*)&te, 0)) ) {
							fprintf(stderr,"Replacing failed: %i\n",st);
						} else {
							printf("Replaced:\n");
							prtble(t, te);
						}
						free(te);
					}
				}
			break;

			case 'f': case 'F':
				if  ( fscanf(f, "%i",&key) > 0 ) {
					te = shtblFind(t, key);
					prtble(t, te);
				}
			break;

			case 'd': case 'D':
				if  ( fscanf(f, "%i",&key) > 0 ) {
					if ( ! (te = shtblFind(t, key)) ) {
						fprintf(stderr,"Key %i not found\n", key);
					} else {
						shtblDel(t, te);
					}
				}
			break;


			case 'q': case 'Q':
			goto bail;
		}

		do { 
			cmd = getchar();
		} while ( -1 != cmd && '\n' != cmd );
	} while ( -1 != cmd );
bail:

	shtblDestroy(t, clnup);

	return 0;
}

#endif
