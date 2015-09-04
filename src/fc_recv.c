/* $Id: fc_recv.c,v 1.10 2010/04/22 18:42:49 strauman Exp $ */

/* 
 * Implementation of the FCOM receiver's high-level parts.
 *
 * Author: Till Straumann <strauman@slac.stanford.edu>, 2009.
 *
 * The code defined in this file requires multithreading (MT)
 * support:
 * 
 *   -  mutual exclusion
 *   -  separate thread (for processing received messages)
 *   -  synchronization (synchronous fcomGetBlob() support).
 *
 * Several (compile-time) options are available:
 *
 *   -  no MT support (for testing purposes). Purely single-threaded
 *      application which executes fcom_receiver() directly.
 * 
 *      This option is chosen by default.
 *
 *   -  use PTHREAD mutexes, threads and condition variables.
 *      This option is enabled if the CPP symbol 'USE_PTHREADS'
 *      is defined.
 *
 *   -  use EPICS mutexes and threads (but the EPICS API provide
 *      no condition variables that can be 'broadcast' to).
 *      This option is enabled if the CPP symbol 'USE_EPICS'
 *      is defined.
 *
 *   -  enable/disable support for synchronous FCOM operation.
 *      This option is enabled if the CPP symbol 'SUPPORT_SYNCGET'
 *      is defined. *Requires* USE_PTHREADS.
 *
 *   -  enable/disable support for blob sets.
 *      This option is enabled if the CPP symbol 'SUPPORT_SETS'
 *      is defined. *Requires* USE_PTHREADS
 *
 * USE_EPICS is probably most portable but provides no support for
 * synchronous FCOM operation. Also, it depends on the EPICS environment
 * and it's libCom (which is the only EPICS library that is *required*
 * for FCOM).
 * 
 * USE_PTHREADS is almost as portable and ensures support for
 * synchronous FCOM operation. An additional advantage is that
 * FCOM is entirely independent of EPICS.
 *
 * SUPPORT_SYNCGET and SUPPORT_SETS add minimal overhead unless
 * the features are actually used (and even if they are they are
 * pretty light-weight).
 */

#if defined(USE_PTHREADS) && defined(USE_EPICS)
#error "cannot compile with both, USE_PTHREADS and USE_EPICS defined"
#endif

#ifdef USE_PTHREADS
#define _XOPEN_SOURCE 500
#endif

#define __INSIDE_FCOM__
#include <fcom_api.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <shtbl.h>
#include <udpComm.h>
#include <fcomP.h>
#include <xdr_dec.h>
#include <sys/time.h>
#include <time.h>
#include <netinet/in.h> /* for htonl & friends only */

#include <config.h>

/* Define multithreading primitives depending on the available API */
#ifdef USE_PTHREADS

#include <pthread.h>
#include <sched.h>

#define __FC_LOCK_DECL(x) \
static pthread_mutex_t fcl_##x;

static void fc_lock_create(pthread_mutex_t *p_l)
{
#ifdef HAVE_PTHREAD_PRIO_INHERIT
int err;
#endif
pthread_mutexattr_t a;
	pthread_mutexattr_init( &a );
	/* older linux implementations don't have PTHREAD_PRIO_INHERIT (but RTEMS does)
	 * makefile magic can detect this and define/undefine HAVE_PTHREAD_PRIO_INHERIT
	 * accordingly.
	 */
#ifdef HAVE_PTHREAD_PRIO_INHERIT
	if ( (err = pthread_mutexattr_setprotocol( &a, PTHREAD_PRIO_INHERIT )) ) {
		fprintf(stderr,
		        "FATAL (FCOM): Unable to set priority inheritance on mutex: %s\n",
                strerror(err)) ;
		abort();
	}
#else
#warning "PTHREAD_PRIO_INHERIT not defined -- mutex priority inheritance not supported on this system"
	if ( ! fcom_silent_mode ) {
		fprintf(stderr,"PTHREAD_PRIO_INHERIT not available on this sytem; using mutex WITHOUT priority inheritance.\n");
	}
#endif
	pthread_mutex_init( p_l, &a );
	pthread_mutexattr_destroy( &a );
}

static __inline__ void
fc_lock(pthread_mutex_t *p_l)
{
int err;
	if ( (err = pthread_mutex_lock( p_l )) ) {
		/* be paranoid and check return value */
		fprintf(stderr,
		        "FATAL (FCOM): Unable to lock mutex: %s\n",
                strerror(err));
		abort();
	} 
}

static __inline__ void
fc_unlock(pthread_mutex_t *p_l)
{
int err;
	if ( (err = pthread_mutex_unlock( p_l )) ) {
		/* be paranoid and check return value */
		fprintf(stderr,
		        "FATAL (FCOM): Unable to unlock mutex: %s\n",
                strerror(err));
		abort();
	} 
}

#define __FC_LOCK_CRE(x)  do { fc_lock_create(&fcl_##x);        } while (0)
#define __FC_LOCK_DEL(x)  do { pthread_mutex_destroy(&fcl_##x); } while (0)

#define __FC_LOCK()       do { fc_lock(&fcl_tbl);   } while (0)
#define __FC_UNLOCK()     do { fc_unlock(&fcl_tbl); } while (0)

#define __FC_LOCK_GRP()   do { fc_lock(&fcl_grp);   } while (0)
#define __FC_UNLOCK_GRP() do { fc_unlock(&fcl_grp); } while (0)

#elif defined(USE_EPICS)

#include <epicsMutex.h>
#include <epicsThread.h>
#include <epicsEvent.h>

#define __FC_LOCK_DECL(x) \
static epicsMutexId fcl_##x;

#define __FC_LOCK_CRE(x)  do { fcl_##x = epicsMutexMustCreate(); } while (0)
#define __FC_LOCK_DEL(x)  do { epicsMutexDestroy( fcl_##x );     } while (0)

#define __FC_LOCK()       do { epicsMutexMustLock( fcl_tbl );    } while (0)
#define __FC_UNLOCK()     do { epicsMutexUnlock  ( fcl_tbl );    } while (0)

#define __FC_LOCK_GRP()   do { epicsMutexMustLock( fcl_grp );    } while (0)
#define __FC_UNLOCK_GRP() do { epicsMutexUnlock  ( fcl_grp );    } while (0)

#else /* no multithreading support */

#warning "No multithreading support enabled!"

#define __FC_LOCK_DECL(x)

#define __FC_LOCK_CRE(x)  do {} while (0)
#define __FC_LOCK_DEL(x)  do {} while (0)

#define __FC_LOCK()       do {} while (0)
#define __FC_UNLOCK()     do {} while (0)

#define __FC_LOCK_GRP()   do {} while (0)
#define __FC_UNLOCK_GRP() do {} while (0)
#endif

#if defined(SUPPORT_SYNCGET) && !defined(USE_PTHREADS)
#warning "synchronous gets are only supported with pthreads"
#undef SUPPORT_SYNCGET
#endif

#if defined(SUPPORT_SETS) && !defined(USE_PTHREADS)
#warning "blob sets are only supported with pthreads"
#undef SUPPORT_SETS
#endif

#ifdef USE_PTHREADS
#include <time.h>
#include <unistd.h> /* for _POSIX_TIMERS */
#endif

/* Macro to verify that an FCOM id is of major protocol version 1 */
#define NOT_V1(idnt) ( FCOM_GET_MAJ(idnt) != FCOM_PROTO_MAJ_1 )

/* Maintain a reference count for multicast groups.
 * The rationale is that any given BSD socket (and
 * udpComm largely emulates BSD semantics) cannot
 * join the same MC group more than once. Hence,
 * in order to implement nesting fcomSubscribe()/fcomUnsubscribe()
 * routines we need to maintain a reference count
 * for MC groups and join only when we subscribe to
 * a given GID for the first time. fcomUnsubscribe()
 * decrements the reference count and leaves the MC
 * group when a reference count of zero is reached.
 */
static uint16_t fc_gid_refcnt[FCOM_GID_MAX+1] = {0};

/*
 * Buffer management.
 * 
 * Buffers consist of a header (for internal management)
 * and a payload (holding a BLOB).
 */

typedef struct Buf *BufRef;

/* A buffer 'header' for maintaining internal data;
 * the 'ptr' member is used to keep buffers on a linked
 * 'free' list while not in use. If the buffer is in-use
 * then the 'ptr' member points to a pthread condition
 * variable which supports synchronous FCOM operation.
 */
typedef struct BufHdr {
	union {
	void           *ptr;
	BufRef         next;           /* linked list of free buffers       */
#if defined(SUPPORT_SYNCGET)
	pthread_cond_t *cond;          /* cond. var. (while buf in use)     */
#endif
	}              ptr;            /* multi-use pointer                 */
	uint16_t       subCnt, refCnt; /* subscription and reference counts */
	uint16_t       size;           /* size of this buffer               */
	uint8_t        type;           /* type of this buffer               */
	uint8_t        setNodeIdx;     /* idx into set node table (if != 0) */
	uint32_t       updCnt;         /* statistics; # of received blobs   */
} BufHdr, *BufHdrRef;

/* A buffer consists of a 'header' and 'payload'-data
 * which shall be aligned to FC_ALIGNMENT.
 */

#define PADALGN ((size_t)FC_ALIGN(sizeof(BufHdr)) - sizeof(BufHdr))

typedef struct Buf {
	BufHdr     hdr;
	uint8_t    pad[PADALGN];  /* align to FC_ALIGNMENT */
	FcomBlob   pld;
} Buf;

/* Chunks of multiple buffers */
typedef struct BufChunk {
	struct BufChunk *next;
	uint8_t          raw[];  /* buffers reside here */
} BufChunk, *BufChunkRef;

/* Blob Sets                  */
#define MAX_SETMEMB          (8*sizeof(FcomBlobSetMask))

#if defined(SUPPORT_SETS)
typedef struct FcomBlobSetHdr {
	FcomBlobSetMask  waitfor;
	FcomBlobSetMask  gotsofar;
	int              waitforall;
	pthread_cond_t   cond;      /* cond. var. (while buf in use)     */
	FcomBlobSet      set;
} FcomBlobSetHdr;

/* Vector table for BlobSetNodes so that we only
 * need 1 byte in a BufHdr
 */
static union {
	FcomBlobSetMemb *node;
	unsigned         next;	/* for linking on a 'free' list */
} setNodeTbl[ 1 << (8*sizeof(((BufHdrRef)0)->setNodeIdx)) ] = {{0}};

#define SET_NODE_TOTAL ((int)(sizeof(setNodeTbl)/sizeof(setNodeTbl[0]) - 1))

static int setNodeAvail = SET_NODE_TOTAL;

#define SET_NODE_FREE_LIST setNodeTbl[0].next

#endif /* SUPPORT_SETS for blob sets */

/* Pools of buffers of different sizes */
static struct {
	BufRef      free_list;
	BufChunkRef chunks;		/* linked-list of chunks of buffers */
	uint16_t    sz;         /* size of this buffer pool         */
	unsigned    tot;        /* stats: tot. # of bufs of this sz */
	unsigned    avail;      /* stats: avail. bufs of this size  */
	unsigned    wght;       /* relative amount at startup       */
} fc_free [] =  {
	{ wght: 4, free_list: 0, chunks: 0, sz:    64, tot: 0, avail: 0 },
	{ wght: 2, free_list: 0, chunks: 0, sz:   128, tot: 0, avail: 0 },
	{ wght: 1, free_list: 0, chunks: 0, sz:   512, tot: 0, avail: 0 },
	{ wght: 1, free_list: 0, chunks: 0, sz:  2048, tot: 0, avail: 0 },
};

#define NBUFKINDS (sizeof(fc_free)/sizeof(fc_free[0]))

/* Some statistics */
static volatile struct {
	uint32_t	bad_msg_version;	/* messages with bad/unknown/unsupported version received */
	uint32_t	bad_blb_version;	/* blobs with bad/unknown/unsupported version received    */
	uint32_t    no_bufs;            /* fc_getb() failed due to lack of free buffers           */
	uint32_t    dec_errs;			/* xdr decoder errors encountered                         */
	uint32_t    n_msg;              /* # of messages/groups received so far                   */
	uint32_t    n_blb;              /* # of blobs received so far                             */
	uint32_t    bad_cond_bcst;		/* # of failed pthread_cond_broadcast()s                  */
#if defined(SUPPORT_SETS)
	uint32_t    n_set;              /* # of sets currently in use                             */
#endif
} fc_stats = {0};


/* A lock for protecting the hash table */
__FC_LOCK_DECL(tbl)
/* A lock for protecting the GID reference count
 * --- this also serializes all subscribe/unsubscribe
 * operations (which don't have to be deterministic)
 */
__FC_LOCK_DECL(grp)

/* Hash table for received buffers indexed by BLOB ID. */
static SHTbl bTbl = 0;

/* Dump buffer-pool statistics to FILE 'f' (must not be NULL) */
static void fc_statb(FILE *f)
{
int i;
unsigned t,a;
	fprintf(f,"FCOM Buffer Statistics:\n");
	for ( i=0; i<NBUFKINDS; i++ ) {
		t = fc_free[i].tot;
		a = fc_free[i].avail;
		fprintf(stderr,"Size %4u: Tot %4u -- Available %4u -- Used %4u\n",
			fc_free[i].sz, t, a, t-a);
	}
}

static __inline__ BufRef
BLOB2BUFR(FcomBlobRef p_blob)
{
	return (BufRef) ( (uintptr_t) (p_blob)
                    - (  (uintptr_t)  &((BufRef)(0))->pld 
                       - (uintptr_t)   (0) ) );
}

/* Obtain a free buffer suitable to hold at least 'sz' of payload data
 * 
 * RETURNS: pointer to new buffer or NULL if none was available.
 *
 * NOTES:   - 'fcl_tbl' lock must be held by caller.
 *          - reference count of buffer is set to 1.
 *          - pointer member in header is set to NULL.
 */
static BufRef
fc_getb(uint16_t sz)
{
int i;
BufRef rval;

	sz += sizeof(Buf);

	for ( i=0; i<NBUFKINDS; i++ ) {
		if ( sz <= fc_free[i].sz ) {
			if ( (rval = fc_free[i].free_list) ) {
				fc_free[i].free_list = rval->hdr.ptr.next;
				fc_free[i].avail--;
				rval->hdr.refCnt     = 1;
				rval->hdr.ptr.ptr    = 0;
				rval->hdr.setNodeIdx = 0;
				return rval;
			}
			/* If no buffer is available try a bigger size */
		}
	}
	return 0;
}

/* Increase buffer reference count by one.
 * 
 * NOTE:    - 'fcl_tbl' lock must be held by caller.
 */
static void
fc_refb(BufRef b)
{
	b->hdr.refCnt++;
}

/* Release buffer.
 *
 * Decrement buffer reference count and enqueue on
 * appropriate 'free-list' when zero count is reached.
 *
 * NOTE:    - 'fcl_tbl' lock must be held by caller.
 */
static void
fc_relb(BufRef b)
{
	if ( 0 == --b->hdr.refCnt ) {
		b->hdr.ptr.next                = fc_free[b->hdr.type].free_list;
		fc_free[b->hdr.type].free_list = b;
		fc_free[b->hdr.type].avail++;
	}
}

/* Add a new chunk of 'n' buffers of type 't' to
 * appropriate free-list. The 'type' identifies the
 * size of the individual new buffers.
 * 
 * NOTE: This routine is thread-safe.
 */
int
fcom_add_bufs(unsigned t, unsigned n)
{
int       i;
uint16_t sz;

	if ( 0 == n )
		return 0;

	if ( t < NBUFKINDS ) {

		/* preallocate and initialize a chunk of buffers */
		BufChunkRef new_chunk;
		void       *ptr;
		BufRef      hd, tl;

		sz = fc_free[t].sz;

		if ( ! (new_chunk = malloc( sizeof(*new_chunk) + n * sz + FC_ALIGNMENT )) ) {
			return FCOM_ERR_NO_MEMORY;
		}

		/* align */
		hd = ptr = (void*) FC_ALIGN(new_chunk->raw);

		/* initialize buffer headers and link them all together
		 * BEFORE acquiring lock.
		 */
		for ( i=0; i<n; i++ ) {
			tl   = ptr;
			ptr += sz;

			memset(tl, 0, sizeof(tl->hdr));
			tl->hdr.ptr.next   = ptr;
			tl->hdr.size       = sz;
			tl->hdr.type       = t;
			tl->hdr.setNodeIdx = 0;
			tl->hdr.subCnt     = 0;
			tl->hdr.refCnt     = 0;
		}
		tl->hdr.ptr.next = 0;

		/* insert chunk and buffers into lists -- this is fast because
		 * the individual buffers have been linked together already...
		 */
		__FC_LOCK();
			new_chunk->next   = fc_free[t].chunks;
			fc_free[t].chunks = new_chunk;
			fc_free[t].avail += n;
			fc_free[t].tot   += n;

			/* enq buffers */
			tl->hdr.ptr.next     = fc_free[t].free_list;
			fc_free[t].free_list = hd;
		__FC_UNLOCK();

		return 0;
	}
	return FCOM_ERR_INTERNAL;
}


/* Remove a buffer subscription.
 *
 * - lookup ID in hash table.
 * - decrement subscription count
 * - if count reaches zero:
 *   o destroy condition variable which supports
 *     synchronous operation if such a variable
 *     had been created.
 *   o remove buffer from hash table
 *   o decrement buffer reference count (matching
 *     initial count of one given by fc_getb()).
 *     
 * RETURNS: zero on success, error code < 0
 *          otherwise.
 * 
 * NOTES: - if this routine fails all internal
 *          state information is left as if it
 *          never had been started.
 *
 *        - the caller must hold the fcl_tbl          
 *          lock during execution of this
 *          routine.
 */
static int
fc_rmbuf(FcomID idnt, void **p_to_free)
{
BufRef buf;
int    err;

	*p_to_free = 0;

	if ( ! (buf = shtblFind( bTbl, idnt )) ) {
		return FCOM_ERR_INVALID_ID;			
	}

	if ( 0 == --buf->hdr.subCnt ) {
		if ( buf->hdr.setNodeIdx ) {
			/* This ID is member of a set; cannot unsubscribe */
			buf->hdr.subCnt++;
			return FCOM_ERR_ID_IN_USE;
		}

#if defined(SUPPORT_SYNCGET)
		if ( buf->hdr.ptr.cond ) {
			/* If they try to unsubscribe a blob that some thread
			 * currently waits for in a synchronous get then
			 * pthread_cond_destroy() returns EBUSY and the 
			 * unsubscribe operation eventually fails.
			 *
			 * Note that we cannot easily defer destruction of
			 * the condvar until after releasing fcl_tbl w/o
			 * creating race conditions. We just hope this is
			 * a reasonably efficient routine...
			 */
			if ( (err = pthread_cond_destroy( buf->hdr.ptr.cond )) ) {
				buf->hdr.subCnt++;
				return FCOM_ERR_SYS(err);
			}
			/* defer 'free()' until after fcl_tbl lock is released */
			*p_to_free = buf->hdr.ptr.cond;
			buf->hdr.ptr.cond = 0;
		}
#endif

		/* deletion cannot fail; we are certain
		 * that the entry exists - otherwise
		 * the condvar may be lost...
		 */
		err = shtblDel( bTbl, buf );
		if ( err ) {
			return FCOM_ERR_INTERNAL;
		}

		fc_relb(buf);
	}

	return 0;
}

/* Release multicast state associated with 'gid'
 *
 *  - decrement reference count for MC address
 *    associated with 'gid'.
 *  - if count reaches zero leave MC group.
 * 
 * RETURNS: zero on success or error status < 0
 *          on failure.
 * 
 * NOTES:   - assumes caller checked 'gid' for validity.
 *          - 'fcl_grp' lock must be held by caller.
 */
static int
fc_relmc(uint32_t gid)
{
int      err, rval;
uint32_t mcaddr;

	rval = 0;

	if ( 0 == --fc_gid_refcnt[gid] ) {
		mcaddr = fcom_g_prefix | htonl(gid);

		if ( (err = udpCommLeaveMcast(fcom_rsd, mcaddr)) ) {
			fc_gid_refcnt[gid] = 1;
			rval               = FCOM_ERR_SYS(-err);
		}
	}

	return rval;
}

/* Subscription as defined by API */
int
fcomSubscribe(FcomID idnt, int supp_sync)
{
int           err;
uint32_t      gid, mcaddr;
BufRef        buf;
FcomBlobRef   pbv1;
void          *garb;

#if !defined(SUPPORT_SYNCGET)
	if ( supp_sync )
		return FCOM_ERR_UNSUPP;
#endif	
	if ( fcom_rsd < 0 ) {
		fprintf(stderr,"fcomSubscribe error: FCOM uninitialized!\nCall fcomInit in st.cmd!\n");
		abort();
	}

	/* hashtable assumes blob V1 layout to locate key */
	if ( NOT_V1(idnt) )
		return FCOM_ERR_BAD_VERSION;

	if ( ! FCOM_ID_VALID(idnt) ) {
		return FCOM_ERR_INVALID_ID;
	}

	gid = FCOM_GET_GID(idnt);

	err = 0;

	/* 'fcl_grp' lock serializes all fcomSubscribe/fcomUnsubscribe
	 * operations. This simplifies the design and has no impact
	 * on latency. Subscription/unsubscription are not deterministic
	 * anyways. Note that the time-critical lock fcl_tbl is
	 * acquired on a more fine-grained basis.
	 */
	__FC_LOCK_GRP();

		__FC_LOCK();
			buf = shtblFind(bTbl, idnt);
			if ( buf ) {
				/* paranoia */
				if ( 0 == fc_gid_refcnt[gid] || 0 == buf->hdr.subCnt ) {
					err = FCOM_ERR_INTERNAL;
				}
			} else {
				/* new entry; must add a empty dummy buffer */
				if ( ( buf = fc_getb(sizeof(FcomBlob)) ) ) {
#ifdef PARANOIA
					memset( &buf->pld, 0, sizeof(FcomBlob) );
#endif
					buf->hdr.subCnt = 0;
					pbv1            = &buf->pld;
					pbv1->fc_vers   = FCOM_PROTO_VERSION;
					pbv1->fc_idnt   = idnt;
					pbv1->fc_type   = FCOM_EL_NONE;

					if ( (err = shtblAdd( bTbl, buf )) ) {
						fc_relb(buf);
						err = FCOM_ERR_NO_MEMORY;
					}
				} else {
					err = FCOM_ERR_NO_MEMORY;
				}
			}

			/* increment subscription count */
			if ( !err ) {
				buf->hdr.subCnt++;
#if defined(SUPPORT_SYNCGET)
				if ( buf->hdr.ptr.cond ) {
					/* if we already have a condvar then there
					 * is no need to create and attach one.
					 */
					supp_sync = 0;
				}
#endif
			}
		__FC_UNLOCK();

		if ( err ) {
			__FC_UNLOCK_GRP();
			return err;
		}

#if defined(SUPPORT_SYNCGET)
		/* Check if we need to create a new condvar */
		if ( supp_sync ) {
			pthread_cond_t *cond;

			/* pre-allocate condvar */
			if ( ! (cond = malloc(sizeof(*cond))) ) {
				err = FCOM_ERR_NO_MEMORY;	
			} else {
				err = pthread_cond_init(cond, 0);
				if ( err ) {
					free(cond);
					cond = 0;
					err = FCOM_ERR_SYS(err);
				}
			}

			garb = 0;

			/* lock buffers and attach condvar */
			__FC_LOCK();
				if ( err ) {
					/* if an error had occurred during creation we
					 * revert the subscription and return with an error.
					 */
					fc_rmbuf(idnt, &garb);
				} else {
					/* must lookup 'buf' again; it might
					 * have been swapped since we released
					 * the fcl_tbl lock during creation of
					 * the condvar.
					 */
					if ( ! (buf = shtblFind(bTbl, idnt)) || buf->hdr.ptr.cond ) {
						/* this should never happen -- nobody could remove
						 * 'idnt' since we still hold the fcl_grp lock and
						 * for the same reason nobody could have attached a
						 * condvar...
						 */
						err = FCOM_ERR_INTERNAL;
					} else {
						/* attach condvar to buffer */
						buf->hdr.ptr.cond = cond;
						cond = 0;
					}
				}
			__FC_UNLOCK();

			/* release left-over garbage outside the locked area;
			 * this can't really happen since IF there is already
			 * a condvar attached from a previous call THEN the
			 * subscription count must have been at least 2 on
			 * entry to fc_rmbuf()...
			 */
			free(garb);

			if ( err ) {
				if ( cond ) {
					pthread_cond_destroy(cond);
					free(cond);
				}
				return err;
			}
		}
#endif

		if ( 0 == fc_gid_refcnt[gid] ) {
			/* must join MC group */

			mcaddr = fcom_g_prefix | htonl(gid);
			if ( (err = udpCommJoinMcast(fcom_rsd, mcaddr)) ) {

				__FC_LOCK();
					fc_rmbuf(idnt, &garb);
				__FC_UNLOCK();

				/* release left-over garbage outside the locked area */
				free(garb);

				__FC_UNLOCK_GRP();
				return FCOM_ERR_SYS(-err);
			}
		}
		fc_gid_refcnt[gid]++;

	__FC_UNLOCK_GRP();

	return 0;
}

/* Unsubscription as defined by API */
int
fcomUnsubscribe(FcomID idnt)
{
int      rval;
uint32_t gid;
void     *garb;

	/* hashtable assumes blob V1 layout to locate key */
	if ( NOT_V1(idnt) )
		return FCOM_ERR_BAD_VERSION;

	if ( ! FCOM_ID_VALID(idnt) ) {
		return FCOM_ERR_INVALID_ID;
	}

	gid = FCOM_GET_GID(idnt);

	/* 'fcl_grp' lock serializes all fcomSubscribe/fcomUnsubscribe
	 * operations. This simplifies the design and has no impact
	 * on latency. Subscription/unsubscription are not deterministic
	 * anyways. Note that the time-critical lock fcl_tbl is
	 * acquired on a more fine-grained basis.
	 */
	__FC_LOCK_GRP();
		__FC_LOCK();
			rval = fc_rmbuf(idnt, &garb);
		__FC_UNLOCK();

		/* release left-over garbage outside the locked area */
		free(garb);

		if ( rval ) {
			__FC_UNLOCK_GRP();
			return rval;
		}

		rval = fc_relmc(gid);

	__FC_UNLOCK_GRP();

	return rval;
}

#if !defined(__rtems__)
#undef ENABLE_PROFILE
#endif

#if defined(ENABLE_PROFILE)
/* Assume difference is always less than 1s */
static __inline__ uint32_t tsdiff(struct timespec *a, struct timespec *b)
{
uint32_t rval = 0;
	if ( a->tv_nsec < b->tv_nsec )
		rval += 1000000000L;
	rval += a->tv_nsec - b->tv_nsec;
	return rval;
}

/* NOTE: lanIpBasic must be built with ENABLE_PROFILE defined, too */
extern void lanIpBscGetBufTstmp(void *b, struct timespec *ts) __attribute__((weak));

#define ADDPROF(i,ts) \
	do {              \
		rtems_clock_get_uptime(&ts); \
		rtems_task_ident(RTEMS_SELF, RTEMS_SEARCH_ALL_NODES, &rx_tid[i]); \
		rx_prof[i] = tsdiff(&ts,&rx_prbs);     \
		rx_prbs = ts;	 \
		i++;             \
	} while (0)
#define PROFBAS(i,b) \
	do {                \
		i = 0;          \
		if ( lanIpBscGetBufTstmp ) { \
			lanIpBscGetBufTstmp(b, &rx_prbs); \
		} else  {       \
			rtems_clock_get_uptime(&rx_prbs); \
		}               \
	} while (0)

struct timespec rx_prbs;
rtems_id rx_tid[20] = {0};
uint32_t rx_prof[20] = {0};
int      rx_prdx     =  0;

#else
#define PROFBAS(i,b)  do {} while(0)
#define ADDPROF(i,ts) do {} while(0)
#endif

static int
ms2timeout(struct timespec *tout, uint32_t timeout_ms)
{
#if _POSIX_TIMERS > 0
int err;
struct timespec now;
#else
struct timeval  now;
#endif

	tout->tv_sec  = timeout_ms / 1000;
	tout->tv_nsec = (timeout_ms - 1000 * tout->tv_sec) * 1000000;

#if _POSIX_TIMERS > 0
	if ( (err = clock_gettime(CLOCK_REALTIME, &now)) ) {
		return FCOM_ERR_SYS(-err); /* returned error is already negative */
	}
	tout->tv_nsec += now.tv_nsec;
#else
	if ( gettimeofday(&now, 0) ) {
		return FCOM_ERR_SYS(errno);
	}
	tout->tv_nsec += now.tv_usec * 1000;
#endif

	tout->tv_sec  += now.tv_sec;

	if ( tout->tv_nsec > 1000000000UL ) {
		tout->tv_nsec -= 1000000000UL;
		tout->tv_sec  += 1;
	}

	return 0;
}


/* Fetch data as defined by API */
int
fcomGetBlob(FcomID idnt, FcomBlobRef *pp_blob, uint32_t timeout_ms)
{
BufRef          buf;
int             rval;
#if defined(SUPPORT_SYNCGET)
struct timespec tout;
#endif

	/* hashtable assumes blob V1 layout to locate key */
	if ( NOT_V1(idnt) )
		return FCOM_ERR_BAD_VERSION;

	if ( ! FCOM_ID_VALID(idnt) )
		return FCOM_ERR_INVALID_ID;

	if ( timeout_ms ) {
#if defined(SUPPORT_SYNCGET)
		/* pre-compute absolute timeout as required by
         * pthread_cond_timedwait()
		 */
		if ( (rval = ms2timeout(&tout, timeout_ms)) )
			return rval;
#else
		return FCOM_ERR_UNSUPP;
#endif
	}

	__FC_LOCK();
#if defined(SUPPORT_SYNCGET)
		if ( timeout_ms ) {
			if ( ! (buf = shtblFind(bTbl, idnt)) ) {
				__FC_UNLOCK();
				return FCOM_ERR_NOT_SUBSCRIBED;
			}
			if ( ! buf->hdr.ptr.cond ) {
				__FC_UNLOCK();
				return FCOM_ERR_NOT_SUBSCRIBED;
			}
			/* block for new data - see the note in fc_rmbuf
			 * for why the buffer/slot we're blocking on here
			 * cannot disappear while we are waiting.
             */
			rval = pthread_cond_timedwait( buf->hdr.ptr.cond, &fcl_tbl, &tout);

			ADDPROF(rx_prdx, tout);

			if ( rval ) {
				rval = ETIMEDOUT == rval ? FCOM_ERR_TIMEDOUT : FCOM_ERR_SYS(rval);
				__FC_UNLOCK();
				return rval;
			}

			/* pthread_cond_timedwait() releases 'fcl_tbl' while blocking;
			 * therefore we must re-fetch 'buf' as done below.
			 */
		}
#endif
		if ( (buf = shtblFind( bTbl, idnt )) ) {
			/* is this a placeholder that was produced
			 * by subscription ?
			 */

			ADDPROF(rx_prdx, tout);

			if ( FCOM_EL_NONE == buf->pld.fc_type ) {
				*pp_blob = 0;
				rval     = FCOM_ERR_NO_DATA;
			} else {
				/* increase buffer reference count before handing
				 * to user.
				 */
				fc_refb(buf);
				*pp_blob = &buf->pld;
				rval     = 0;
			}
			ADDPROF(rx_prdx, tout);
		} else {
			rval = FCOM_ERR_NOT_SUBSCRIBED;
		}
	__FC_UNLOCK();
	ADDPROF(rx_prdx, tout);

	return rval;
}

/* Release blob reference as defined by API */
int
fcomReleaseBlob(FcomBlobRef *pp_blob)
{
BufRef buf;

	if ( 0 == *pp_blob )
		return 0;

	/* use some magic to compute the 'buf' pointer */
	buf = BLOB2BUFR(*pp_blob);

	__FC_LOCK();
		fc_relb(buf);
	__FC_UNLOCK();

	*pp_blob = 0;
	return 0;
}

int
fcomAllocBlobSet(FcomID member_id[], unsigned num_members, FcomBlobSetRef *pp_set)
{
int                rval = 0;
#if defined(SUPPORT_SETS)
int                i, j;
int                nodes_needed;
FcomBlobSetHdrRef  aset = 0;
BufHdrRef          buf;

	/* basic check on arguments */

	if ( ! pp_set )
		return FCOM_ERR_INVALID_ARG;

	*pp_set = 0; /* paranoia setting */

	/* Need always at least 1 member */
	if ( num_members < 1 || num_members > MAX_SETMEMB )
		return FCOM_ERR_INVALID_COUNT;

	for ( j = 0; j < num_members; j++ ) {
		if ( NOT_V1(member_id[j]) )
			return FCOM_ERR_BAD_VERSION;

		if ( ! FCOM_ID_VALID(member_id[j]) ) {
			return FCOM_ERR_INVALID_ID;
		}

		for ( i=0; i<j; i++ ) {
			/* We assume all IDs are different */
			if ( member_id[i] == member_id[j] ) {
				return FCOM_ERR_INVALID_ARG;
			}
		}
	}

	/* reserve space for header and member structs */
	i = sizeof(*aset) + num_members * sizeof(aset->set.memb[0]);

	if ( ! (aset = malloc(i)) ) {
		return FCOM_ERR_NO_MEMORY;
	}

	memset(aset, 0, i);

	rval = pthread_cond_init(&aset->cond, 0);

	if ( rval ) {
		free(aset);
		return FCOM_ERR_SYS(rval);
	}

	/* Use group lock to protect sets and also to make sure
	 * subscriptions don't change while we're manipulating the set.
	 */
	nodes_needed = 0;

	__FC_LOCK_GRP();
			/* Verify that all IDs are subscribed and count
			 * number of nodes required. Nobody else can mess
			 * with nodes since we're holding the group lock!
			 */
			for ( i=0; i<num_members; i++ ) {
					__FC_LOCK();
					buf = shtblFind(bTbl, member_id[i]);
					if ( buf ) {
						if ( 0 == buf->setNodeIdx )
							nodes_needed++;
						/* else: a node table slot has already been allocated for this buf */
						__FC_UNLOCK();
					} else {
						__FC_UNLOCK();
						rval = FCOM_ERR_NOT_SUBSCRIBED;
						goto bail;
					}
			}

			if ( nodes_needed > setNodeAvail ) {
				rval = FCOM_ERR_NO_SPACE;
				goto bail;
			}

			setNodeAvail    -= nodes_needed;

			aset->waitfor    = 0;
			aset->gotsofar   = 0;
			aset->waitforall = 0;

			aset->set.nmemb  = num_members;

			for ( i=0; i<num_members; i++ ) {
					aset->set.memb[i].idnt = member_id[i];
					aset->set.memb[i].blob = 0;
					aset->set.memb[i].head = aset;
					/* Enqueue in member list */
					__FC_LOCK();
					buf = shtblFind(bTbl, member_id[i]);
					if ( buf ) {
							if ( 0 == buf->setNodeIdx ) {
									/* Not yet member of any group; allocate
									 * a pointer from the vector table.
									 */
									buf->setNodeIdx    = SET_NODE_FREE_LIST;
									SET_NODE_FREE_LIST = setNodeTbl[SET_NODE_FREE_LIST].next;
									setNodeTbl[buf->setNodeIdx].node = 0;
							}
							/* enqueue into list of memberships of this buf */
							aset->set.memb[i].next           = setNodeTbl[buf->setNodeIdx].node;
							setNodeTbl[buf->setNodeIdx].node = &aset->set.memb[i];
					} else {
							fprintf(stderr,"FATAL (FCOM): BUF DISAPPEARED??\n");
							fflush(stderr);
							abort();
					}
					__FC_UNLOCK();
			}

			fc_stats.n_set++;

			*pp_set = &aset->set;
			aset    = 0;
bail:
	__FC_UNLOCK_GRP();

	if ( aset ) {
		pthread_cond_destroy( &aset->cond );
		free( aset );
	}
#else
	rval = FCOM_ERR_UNSUPP;
#endif
	return rval;
}

int
fcomFreeBlobSet(FcomBlobSetRef p_set)
{
int                rval;
#if defined(SUPPORT_SETS)
int                i;
FcomBlobSetHdrRef  aset;
BufHdrRef          buf;
FcomBlobSetMembRef *p_m;

	if ( ! p_set )
		return 0;

	aset = p_set->memb[0].head; /* There must be at least one member */

	__FC_LOCK_GRP();

	__FC_LOCK();
	/* If a 'fcomGetBlobSet()' operation is in progress this fails with EBUSY */
	rval = pthread_cond_destroy( &aset->cond );
	__FC_UNLOCK();

	if ( rval ) {
		__FC_UNLOCK_GRP();
		return FCOM_ERR_SYS(rval);
	}
	
	for ( i=0; i<aset->set.nmemb; i++ ) {
		__FC_LOCK();
			/* If there is still blob attached then release it */
			if ( aset->set.memb[i].blob ) {
				fc_relb( BLOB2BUFR( aset->set.memb[i].blob ) );
			}
			buf = shtblFind(bTbl, aset->set.memb[i].idnt);
			if ( ! buf ) {
				fprintf(stderr,"FATAL (FCOM): MEMBER OF A SET DISAPPEARED??\n");
				fflush(stderr);
				abort();
			}
			if ( 0 == buf->setNodeIdx ) {
				fprintf(stderr,"FATAL (FCOM): MEMBER OF A SET HAS 0 setNodeIdx??\n");
				fflush(stderr);
				abort();
			}
			/* Look for this member and remove */
			for ( p_m = &setNodeTbl[buf->setNodeIdx].node; *p_m; p_m = &(*p_m)->next ) {
				if ( *p_m == &aset->set.memb[i] ) {
					/* found */
					*p_m = aset->set.memb[i].next;
					aset->set.memb[i].next = 0; /* paranoia */
					/* no point in continuing */
					break;
				}
			}
			/* If this is the last set membership release vector table slot */
			if ( 0 == setNodeTbl[buf->setNodeIdx].node ) {
				setNodeTbl[buf->setNodeIdx].next = SET_NODE_FREE_LIST;
				SET_NODE_FREE_LIST               = buf->setNodeIdx;
				setNodeAvail++;
				buf->setNodeIdx                  = 0;
			}
		__FC_UNLOCK();
	}

	fc_stats.n_set--;

	__FC_UNLOCK_GRP();

	free( aset );

#else
	rval = FCOM_ERR_UNSUPP;
#endif

	return rval;
}

int
fcomGetBlobSet(FcomBlobSetRef p_set, FcomBlobSetMask *p_res, FcomBlobSetMask waitfor, int flags, uint32_t timeout_ms)
{
int               rval;
#if defined(SUPPORT_SETS)
FcomBlobSetHdrRef aset;
struct timespec   tout;

	if ( ! p_set || ! waitfor || 0 == timeout_ms ) {
		return FCOM_ERR_INVALID_ARG;
	}

	/* There is at least one member */
	aset = p_set->memb[0].head;

	/* pre-compute timeout */
	if ( (rval = ms2timeout( &tout, timeout_ms )) ) {
		return rval;
	}

	__FC_LOCK();

		aset->waitfor    = waitfor;
		aset->waitforall = (FCOM_SET_WAIT_ALL & flags);
		aset->gotsofar   = 0;

		rval = pthread_cond_timedwait( &aset->cond, &fcl_tbl, &tout);

		/* reset 'waitfor'. If we timed out we don't want to
		 * get any 'late' data.
		 */
		aset->waitfor    = 0;

	__FC_UNLOCK();

	if ( p_res )
		*p_res = aset->gotsofar;

	if ( rval )
		rval = ETIMEDOUT == rval ? FCOM_ERR_TIMEDOUT : FCOM_ERR_SYS(rval);

#else
	rval = FCOM_ERR_UNSUPP;
#endif
	return rval;
}


static const char *t2s(uint8_t t)
{
	switch ( FCOM_EL_TYPE(t) ) {
		case FCOM_EL_FLOAT:  return "flt";
		case FCOM_EL_DOUBLE: return "dbl";
		case FCOM_EL_UINT32: return "u32";
		case FCOM_EL_INT32:  return "i32";
		case FCOM_EL_INT8:   return "i08";
		default:
		break;
	}
	return "***";
}

int
fcomDumpBlob(FcomBlobRef blob, int level, FILE *f)
{
BufRef buf;
int    i;
int    rval = 0;

	if ( ! f )
		f = stdout;

	if ( !blob ) {
		return FCOM_ERR_INVALID_ARG;
	}

	buf = BLOB2BUFR( blob );

	rval += fprintf(f,"Statistics for FCOM ID 0x%08"PRIx32":\n", buf->pld.fc_idnt);
	rval += fprintf(f,"  Subscriptions :       %4u\n",           buf->hdr.subCnt);
	rval += fprintf(f,"  Buffer updates:       %4"PRIu32"\n",    buf->hdr.updCnt);
	if ( level > 0 ) {
		rval += fprintf(f,"  Buffer size   :       %4u\n",           buf->hdr.size);
		rval += fprintf(f,"  Buffer refcnt :       %4u\n",           buf->hdr.refCnt);
	}
	if ( level > 0 || buf->hdr.setNodeIdx ) {
		rval += fprintf(f,"  Blobset member: ");
		if ( buf->hdr.setNodeIdx ) {
			rval += fprintf(f, "YES [@%3u]\n", buf->hdr.setNodeIdx);
		} else {
			rval += fprintf(f, "      NONE\n");
		}
	}

	if ( FCOM_EL_NONE == buf->pld.fc_type ) {
		rval += fprintf(f,"  Blob payload  : ***NO DATA RECEIVED***\n");
	} else {
		if ( level > 0 ) {
			rval += fprintf(f,"  Proto version :       0x%02x\n",        buf->pld.fc_vers);
		}
		rval += fprintf(f,"  Blob status   : 0x%08"PRIx32"\n",       buf->pld.fc_stat);
		rval += fprintf(f,"  Blob timestmpH: 0x%08"PRIx32"\n",       buf->pld.fc_tsHi);
		rval += fprintf(f,"  Blob timestmpL: 0x%08"PRIx32"\n",       buf->pld.fc_tsLo);
		rval += fprintf(f,"  Blob payload  :   %s[%3u]\n", t2s(buf->pld.fc_type), buf->pld.fc_nelm);
		if ( level > 0 ) {
			for ( i=0; i<buf->pld.fc_nelm; i++ ) {
				switch ( FCOM_EL_TYPE(buf->pld.fc_type) ) {
					default:             i = buf->pld.fc_nelm;
					break;

					case FCOM_EL_FLOAT:  rval += fprintf(f,"    %.4e\n",
												           buf->pld.fc_flt[i]
					                                    );
					break;

					case FCOM_EL_DOUBLE: rval += fprintf(f,"    %.6e\n",
												           buf->pld.fc_dbl[i]
					                                    );
					break;

					case FCOM_EL_UINT32: rval += fprintf(f,"    0x%08"PRIx32"(%9"PRIu32")\n",
												           buf->pld.fc_u32[i],
												           buf->pld.fc_u32[i]
												        );
					break;

					case FCOM_EL_INT32:  rval += fprintf(f,"    0x%08"PRIx32"(%10"PRIi32")\n",
												           buf->pld.fc_i32[i],
												           buf->pld.fc_i32[i]
												        );
					break;

					case FCOM_EL_INT8:   rval += fprintf(f,"    0x%02"PRIx8"(%4"PRIi8")\n",
												           (uint8_t)buf->pld.fc_i08[i],
												           buf->pld.fc_i08[i]
											            );
					break;
				}
			}
		}
	}

	return rval;
}

int
fcomDumpIDStats(FcomID idnt, int level, FILE *f)
{
BufRef buf;
int    rval = 0;

	if ( ! f )
		f = stdout;

	__FC_LOCK();

	if ( (buf = shtblFind( bTbl, idnt )) ) {
		fc_refb(buf);
	} 

	__FC_UNLOCK();

	if ( ! buf ) {
		return fprintf(f,"fcomDumpIDStats: %s\n", fcomStrerror(FCOM_ERR_NOT_SUBSCRIBED));
	}

	rval = fcomDumpBlob( &buf->pld, level, f );

	__FC_LOCK();
		fc_relb(buf);
	__FC_UNLOCK();

	return rval;
}

volatile int fcom_recv_running = 1;

void
fcom_recv_stats(FILE *f)
{
unsigned sz,n;

	if ( !f )
		f = stdout;
	fc_statb(f);
	fprintf(f, "FCOM Rx Statistics:\n");
	fprintf(f, "  messages with unsupported version received: %4"PRIu32"\n",
               fc_stats.bad_msg_version);
	fprintf(f, "  blobs with unsupported version received:  %6"PRIu32"\n",
               fc_stats.bad_blb_version);
	fprintf(f, "  failed to allocate buffer:                %6"PRIu32"\n",
               fc_stats.no_bufs);
	fprintf(f, "  XDR decoding errors:                      %6"PRIu32"\n",
               fc_stats.dec_errs);
	fprintf(f, "  messages processed:                    %9"PRIu32"\n",
               fc_stats.n_msg);
	fprintf(f, "  blobs processed:                       %9"PRIu32"\n",
               fc_stats.n_blb);
	fprintf(f, "  failed syncget or set member bcasts:   %9"PRIu32"\n",
               fc_stats.bad_cond_bcst);
#if defined(SUPPORT_SETS)
	fprintf(f, "  set vector table entries available: %3u (of %3u)\n",
	           setNodeAvail, SET_NODE_TOTAL);
	fprintf(f, "  allocated blob sets:                   %9"PRIu32"\n",
	           fc_stats.n_set);
#else
	fprintf(f, "  allocated blob sets:  UNSUPPORTED (NOT COMPILED)\n");
#endif
	if ( bTbl ) {
		shtblStats(bTbl, &sz, &n);
	fprintf(f, "  hash table size/entries/load: %u/%u/%.0f%%\n",
	           sz, n, (float)n/(float)sz*100.0);
	}
}

int
fcom_get_rx_stat(uint32_t key, uint64_t *p_val)
{
uint32_t v;
unsigned sz, nused;
unsigned kind = FCOM_STAT_KIND(key);
	switch ( key & ~kind ) {
		case FCOM_STAT_RX_NUM_BLOBS_RECV:
			v = fc_stats.n_blb;
		break;

		case FCOM_STAT_RX_NUM_MESGS_RECV:
			v = fc_stats.n_msg;
		break;

		case FCOM_STAT_RX_ERR_NOBUF:
			v = fc_stats.no_bufs;
		break;

		case FCOM_STAT_RX_ERR_XDRDEC:
			v = fc_stats.dec_errs;
		break;

		case FCOM_STAT_RX_ERR_BAD_BVERS:
			v = fc_stats.bad_blb_version;
		break;

		case FCOM_STAT_RX_ERR_BAD_MVERS:
			v = fc_stats.bad_msg_version;
		break;

		case FCOM_STAT_RX_ERR_BAD_BCST:
			v = fc_stats.bad_cond_bcst;
		break;

		case FCOM_STAT_RX_NUM_BLOBS_SUBS:
			shtblStats(bTbl, &sz, &nused);
			v = nused;
		break;

		case FCOM_STAT_RX_NUM_BLOBS_MAX:
			shtblStats(bTbl, &sz, &nused);
			v = sz;
		break;

		case FCOM_STAT_RX_NUM_BUF_KINDS:
			v = NBUFKINDS;
		break;

		case FCOM_STAT_RX_BUF_SIZE(0):
			if ( kind >= NBUFKINDS ) return FCOM_ERR_UNSUPP;
			v = fc_free[kind].sz;
		break;

		case FCOM_STAT_RX_BUF_NUM_TOT(0):
			if ( kind >= NBUFKINDS ) return FCOM_ERR_UNSUPP;
			v = fc_free[kind].tot;
		break;

		case FCOM_STAT_RX_BUF_NUM_AVL(0):
			if ( kind >= NBUFKINDS ) return FCOM_ERR_UNSUPP;
			v = fc_free[kind].avail;
		break;

		case FCOM_STAT_RX_BUF_ALIGNED(0):
			if ( kind >= NBUFKINDS ) return FCOM_ERR_UNSUPP;
			v = FC_ALIGNMENT;
		break;

		default: 
		return FCOM_ERR_UNSUPP;
	}
	*p_val = v;
	return 0;
}

/* Receive and process a single message/group
 * 
 * RETURNS: number of blobs decoded (0 if timed out).
 */
int
fcom_receive(unsigned timeout_ms)
{
UdpCommPkt         p;
uint32_t           *xmemp;
int                i,nblobs,sz,xsz;
BufRef             buf,obuf;
FcomID             idnt;
#if defined(SUPPORT_SETS)
FcomBlobSetHdrRef  aset;
FcomBlobSetMembRef amemb;
FcomBlobSetMask    wanted;
FcomBlobSetMask    me;
#endif

#ifdef ENABLE_PROFILE
struct timespec tstmp;
#endif

	nblobs = 0;

	/* Block for a packet */
	if ( (p = udpCommRecv(fcom_rsd, timeout_ms)) ) {

			xmemp = udpCommBufPtr(p);

		PROFBAS(rx_prdx, p);
		ADDPROF(rx_prdx, tstmp); 

			/* decode message header */
			if ( (sz = fcom_xdr_dec_msghdr(xmemp, &nblobs)) > 0 ) {

		ADDPROF(rx_prdx, tstmp); 
				fc_stats.n_msg++;

				for (i=0, xmemp+=sz; i < nblobs; i++) {

					fc_stats.n_blb++;

					/* extract ID and size information up-front */
					xsz = fcom_xdr_peek_size_id(&sz, &idnt, xmemp);
		ADDPROF(rx_prdx, tstmp); 
					if ( xsz < 0 ) {
						fc_stats.bad_blb_version++;
						goto bail;
					}

					/* check for this ID -- if it is not subscribed
					 * then we can simply skip ahead.
					 */
					__FC_LOCK();
						if ( (obuf = shtblFind(bTbl, idnt)) ) {
							/* found; ID is apparently subscribed. We
							 * allocate a buffer for the new data.
							 */
							buf = fc_getb(sz);
						} else {
							/* not found; this ID is not subscribed */
							buf = 0;
						}
					__FC_UNLOCK();
		ADDPROF(rx_prdx, tstmp); 

					if ( obuf && !buf ) {
						/* account for failure to get a new buffer above */
						fc_stats.no_bufs++;
					}

					/* if we have no buffer (ID not subscribed or no memory)
					 * then skip this blob.
					 */
					if ( buf ) {
						/* decode; note that we run the decoder w/o holding the lock.
						 * Therefore it could happen that somebody unsubscribes while
						 * we are working.
						 */
						if ( fcom_xdr_dec_blob( &buf->pld, sz, xmemp) > 0 ) {
		ADDPROF(rx_prdx, tstmp); 
							__FC_LOCK();
							/* have to check again if this ID is still subscribed */
							obuf = buf;
							if ( 0 == shtblRpl(bTbl, (SHTblEntry*)&obuf, SHTBL_ADD_FAIL) ) {
		ADDPROF(rx_prdx, tstmp); 
								/* old entry was replaced by 'buf'; 'obuf' contains
								 * reference to old entry.
								 *
								 * Increment statistics counter and copy
								 * the subscription count and cond-var
                                 * into the new buffer.
								 */
								buf->hdr.updCnt      = obuf->hdr.updCnt + 1;
								buf->hdr.subCnt      = obuf->hdr.subCnt;
								buf->hdr.setNodeIdx  = obuf->hdr.setNodeIdx;
								obuf->hdr.setNodeIdx = 0;

#if defined(SUPPORT_SYNCGET)
								buf->hdr.ptr.cond    = obuf->hdr.ptr.cond;
								obuf->hdr.ptr.cond   = 0;

								if ( buf->hdr.ptr.cond ) {
									/* post to blocked clients */
		ADDPROF(rx_prdx, tstmp); 
									if ( pthread_cond_broadcast( buf->hdr.ptr.cond ) ) {
										fc_stats.bad_cond_bcst++;
									}
		ADDPROF(rx_prdx, tstmp); 
								}
#endif
#if defined(SUPPORT_SETS)
								if ( buf->hdr.setNodeIdx ) {
		ADDPROF(rx_prdx, tstmp); 
									for ( amemb = setNodeTbl[buf->hdr.setNodeIdx].node;
									      amemb;
									      amemb = amemb->next
									    ) {
										aset = amemb->head;
										me   = (1<<(amemb - aset->set.memb));
										if ( ! (aset->waitfor & me) )
											continue; /* I'm not wanted */

										/* release blob/buf already attached to the set  */
										if ( amemb->blob )
											fc_relb( BLOB2BUFR( amemb->blob ) );

										/* attach this blob/buf and bump reference count */
										amemb->blob     = &buf->pld;
										fc_refb( buf );

										aset->gotsofar |= me;

										/* AND operation not really necessary since
										 * all 'me's have been tested against waitfor
										 */
										wanted          = aset->gotsofar & aset->waitfor;
										if (   ( aset->waitforall && (wanted == aset->waitfor) )
										    || ( !aset->waitforall && wanted ) ) {
											if ( pthread_cond_broadcast(&aset->cond) ) {
												fc_stats.bad_cond_bcst++;
											} else {
												/* Disable further updates */
												aset->waitfor = 0;
											}
										}
									}
		ADDPROF(rx_prdx, tstmp); 
								}
#endif
							}
							/* in the unlikely case that the ID had been unsubscribed
							 * since our previous check above the variable 'obuf' is
							 * still equal to 'buf' and this call hence releases the
							 * buffer we filled in vain...
							 */
							fc_relb(obuf);
		ADDPROF(rx_prdx, tstmp); 
							__FC_UNLOCK();
						} else {
							fc_stats.dec_errs++;
						}
					}
					/* advance XDR stream pointer */
					xmemp += xsz;
				}
			} else {
				fc_stats.bad_msg_version++;
			}
bail:
			udpCommFreePacket(p);
		}

		return nblobs;
}

#if defined(USE_PTHREADS) || defined(USE_EPICS)

#if defined(USE_PTHREADS)
#define TASKRTN_TYPE void*
#elif defined(USE_EPICS)
#define TASKRTN_TYPE void
#endif

#ifdef USE_EPICS
static epicsEventId fc_term_sync = 0;
#endif

/* Receiver task function which periodically polls
 * a 'termination' flag.
 */
static TASKRTN_TYPE
fc_recvr(void *arg)
{
	while ( fcom_recv_running ) {
		fcom_receive(500);
	}
	fcom_recv_running = 1;

#ifdef USE_EPICS
	epicsEventSignal(fc_term_sync);
#endif

#ifdef USE_PTHREADS
	return 0;
#endif
}
#endif

#ifdef USE_PTHREADS
static pthread_t fc_recvr_tid;
static int       fc_recvr_started = 0;

/* Start receiver task (PTHREAD version) */
static void
fc_recvr_start(int prio_pcnt)
{
int                err, prio, pmin, pmax;
pthread_attr_t     atts;
const char         *msg;
struct sched_param pri;

	if ( (err = pthread_attr_init(&atts)) ) {
		msg="pthread_attr_init";
		goto bail;
	}

	/* we want to join this thread which gives us a convenient way
	 * to synchronize with its termination.
	 * However, JOINABLE should be the default anyways...
	 */
	if ( (err = pthread_attr_setdetachstate(&atts, PTHREAD_CREATE_JOINABLE)) ) {
		msg="pthread_attr_setdetachstate";
		goto bail;
	}

	/* Need to set EXPLICIT_SCHED -- otherwise the policy and priority attributes
	 * are inherited and the settings we give are ignored.
	 */
	if ( (err = pthread_attr_setinheritsched(&atts, PTHREAD_EXPLICIT_SCHED)) ) {
		msg="pthread_attr_setinheritsched";
		goto bail;
	}

	if ( (err = pthread_attr_setschedpolicy(&atts, SCHED_FIFO)) ) {
		msg="pthread_attr_setschedpolicy";
		goto bail;
	}

	pmin = sched_get_priority_min(SCHED_FIFO);
	pmax = sched_get_priority_max(SCHED_FIFO);

	if ( pmin < 0 || pmax < 0 ) {
		err = errno;
		msg="sched_get_priority_min/max";
		goto bail;
	}

	prio = (pmax - pmin) * prio_pcnt;
	prio = pmin + prio/100;

	pri.sched_priority = prio;

	if ( (err = pthread_attr_setschedparam(&atts, &pri)) ) {
		msg="pthread_attr_setschedparam";
		goto bail;
	}

	if ( (err = pthread_create(&fc_recvr_tid, &atts, fc_recvr, 0)) ) {

		/* if we aren't allowed to use RT scheduling then issue
		 * a warning and try without...
		 */
		if ( EPERM == err ) {
			if ( ! fcom_silent_mode ) {
				fprintf(stderr,"Warning (FCOM): NOT using real-time scheduler \n");
				fprintf(stderr,"                due to lack of privilege (EPERM)\n");
			}
			if ( (err = pthread_attr_setinheritsched(&atts, PTHREAD_INHERIT_SCHED)) ) {
				msg="pthread_attr_setinheritsched";
				goto bail;
			}

			err = pthread_create(&fc_recvr_tid, &atts, fc_recvr, 0);
		}
		if ( err ) {
			msg="pthread_create";
			goto bail;
		}
	}

	fc_recvr_started = 1;

	return;

bail:
	fprintf(stderr,"FATAL (FCOM) %s failed: %s\n", msg, strerror(err));
	abort();
}

/* Stop receiver task (PTHREAD version) */
static void
fc_recvr_stop()
{
	if ( fc_recvr_started ) {
		/* clear 'fcom_recv_running'; RX task will terminate
		 * its loop when it polls this flag for the next time.
		 */
		fcom_recv_running = 0;
		/* block until RX task terminates */
		pthread_join(fc_recvr_tid, 0);
		fc_recvr_started = 0;
	}
}
#elif defined(USE_EPICS)
static epicsThreadId fc_recvr_tid = 0;

/* Starting a task is easier using the EPICS API... */
static void
fc_recvr_start(int prio_pcnt)
{
int prio;
int stacksz;

	prio = (epicsThreadPriorityMax - epicsThreadPriorityMin) * prio_pcnt;
	prio = prio/100 + epicsThreadPriorityMin;

	stacksz = epicsThreadGetStackSize( epicsThreadStackMedium );

	fc_recvr_tid = epicsThreadMustCreate("fcomRX", prio, stacksz, fc_recvr, 0);
}

/* Stopping the task requires an extra synchronization device */
static void
fc_recvr_stop()
{
	if ( fc_recvr_tid ) {
		fc_term_sync = epicsEventMustCreate(epicsEventEmpty);
		fcom_recv_running = 0;
		epicsEventMustWait(fc_term_sync);
		epicsEventDestroy(fc_term_sync);
		fc_recvr_tid = 0;
		fc_term_sync = 0;
	}
}
#endif

/* FCOM Receiver initialization */
int
fcom_recv_init(unsigned nbufs)
{
int       i,rval;
uintptr_t key_off,n;

	if ( nbufs == 0 )
		nbufs = 1000;

	/* Create locks */
	__FC_LOCK_CRE(tbl);
	__FC_LOCK_CRE(grp);

	for ( n=i=0; i<NBUFKINDS; i++ ) {
		n += fc_free[i].wght;		
	}

	/* Create buffers -- it is easy to add more buffers at run-time
	 * but re-sizing the hash table (any hash table) is not easy and
	 * may be time-consuming.
	 */
	for ( i = 0; i<NBUFKINDS; i++ ) {
		if ( (rval = fcom_add_bufs(i, (nbufs * fc_free[i].wght)/n)) )
			return rval;
	}

	/* Create hash table */
	key_off =   (uintptr_t) &((BufRef)0)->pld.fc_idnt
              - (uintptr_t)  ((BufRef)0);

	if ( ! ( bTbl = shtblCreate(4 * nbufs, (unsigned long)key_off) ) ) {
		fprintf(stderr,"Fatal Error: Unable to create FCOM hash table\n");
		return FCOM_ERR_INTERNAL;
	}

#if defined(SUPPORT_SETS)
	/* Initialize set node table; link everything on 'free' list;
	 * since index 0 in the BufHdr is reserved (means: not member
	 * of any set) we use this as the anchor for the free list.
	 */
	SET_NODE_FREE_LIST = 0;
	for ( i = 1; i< sizeof(setNodeTbl)/sizeof(setNodeTbl[0]); i++ ) {
		setNodeTbl[i].next = SET_NODE_FREE_LIST;
		SET_NODE_FREE_LIST = i;
	}

	setNodeAvail = i - 1;
#endif

	/* Start receiver */
#if defined(USE_PTHREADS) || defined(USE_EPICS)
	fc_recvr_start(fcom_rx_priority_percent);
#endif
	return 0;
}

static void fc_buf_cleanup(SHTblEntry e, void *closure)
{
	fc_relmc(FCOM_GET_GID( ((BufRef)e)->pld.fc_idnt));
	fc_relb(e);
}

/* FCOM Receiver cleanup (in reverse order) */
int
fcom_recv_fini()
{
int         i;
unsigned    d;
BufChunkRef r,p;

	/* Stop task */
#if defined(USE_PTHREADS) || defined(USE_EPICS)
	fc_recvr_stop();
#endif

	/* Destroy hash table. shtblDestroy() scans the
	 * entire table and calls fc_buf_cleanup() on all
	 * non-NULL/left-over entries.
	 */
	if ( bTbl ) {
		__FC_LOCK_GRP();
		__FC_LOCK();
		shtblDestroy(bTbl, fc_buf_cleanup, 0);
		bTbl = 0;
		__FC_UNLOCK();
		__FC_UNLOCK_GRP();
	}

	/* Release buffer memory - this fails if there are any
	 * references to buffers (e.g., in the application).
	 */
	for ( i = 0; i<NBUFKINDS; i++ ) {
		if ( 0 == fc_free[i].tot )
			continue;

		__FC_LOCK();
		d = fc_free[i].tot - fc_free[i].avail;
		if ( 0 != d ) {
			fprintf(stderr, 
					"%u buffers (out of %u) of size %"PRIu16" still in use\n",
					d, fc_free[i].tot, fc_free[i].sz);
			__FC_UNLOCK();
			return FCOM_ERR_INTERNAL;
		}

		for ( r = fc_free[i].chunks; r; ) {
			p = r;
			r = p->next;
			free(p);
		} 
		fc_free[i].free_list = 0;
		fc_free[i].chunks    = 0;
		fc_free[i].tot       = 0;
		fc_free[i].avail     = 0;

		__FC_UNLOCK();
	}
	__FC_LOCK_DEL(tbl);
	__FC_LOCK_DEL(grp);
	return 0;
}
