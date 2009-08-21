/* $Id: fc_recv.c,v 1.3 2009/08/21 03:09:36 strauman Exp $ */

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
 * USE_EPICS is probably most portable but provides no support for
 * synchronous FCOM operation. Also, it depends on the EPICS environment
 * and it's libCom (which is the only EPICS library that is *required*
 * for FCOM).
 * 
 * USE_PTHREADS is almost as portable and ensures support for
 * synchronous FCOM operation. An additional advantage is that
 * FCOM is entirely independent of EPICS.
 *
 * SUPPORT_SYNCGET adds minimal overhead unless the feature is
 * actually used.
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
	fprintf(stderr,"PTHREAD_PRIO_INHERIT not available on this sytem; using mutex WITHOUT priority inheritance.\n");
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
	uint8_t        flags;          /* currently unused                  */
} BufHdr, *BufHdrRef;

/* A buffer consists of a 'header' and 'payload'-data
 * which shall be aligned to FC_ALIGNMENT.
 */

#define PADALGN ((size_t)FC_ALIGN(sizeof(BufHdr)) - sizeof(BufHdr))

typedef struct Buf {
	BufHdr     hdr;
	uint8_t    pad[PADALGN];  /* align to FC_ALIGNMENT */
	uint8_t    pld[];         /* payload               */
} Buf;

/* Chunks of multiple buffers */
typedef struct BufChunk {
	struct BufChunk *next;
	uint8_t          raw[];  /* buffers reside here */
} BufChunk, *BufChunkRef;

/* Pools of buffers of different sizes */
static struct {
	BufRef      free_list;
	BufChunkRef chunks;		/* linked-list of chunks of buffers */
	uint16_t    sz;         /* size of this buffer pool         */
	unsigned    tot;        /* stats: tot. # of bufs of this sz */
	unsigned    avail;      /* stats: avail. bufs of this size  */
	unsigned    wght;       /* relative amount at startup       */
} fc_free [] =  {
	{ wght: 4,   free_list: 0, chunks: 0, sz:    64, tot: 0, avail: 0 },
	{ wght: 2,  free_list: 0, chunks: 0, sz:   128, tot: 0, avail: 0 },
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
#ifdef SUPPORT_SYNCGET
	uint32_t    bad_cond_bcst;		/* # of failed pthread_cond_broadcast()s                  */
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
				rval->hdr.refCnt  = 1;
				rval->hdr.ptr.ptr = 0;
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
			tl->hdr.flags      = 0;
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
				return FCOM_ERR_SYS(-err);
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
					memset( buf->pld, 0, sizeof(FcomBlob) );
#endif
					buf->hdr.subCnt = 0;
					pbv1            = (FcomBlobRef)buf->pld;
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
					err = FCOM_ERR_SYS(-err);
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
int      err;
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
			err = fc_rmbuf(idnt, &garb);
		__FC_UNLOCK();

		/* release left-over garbage outside the locked area */
		free(garb);

		if ( err ) {
			__FC_UNLOCK_GRP();
			return err;
		}

		err = fc_relmc(gid);

	__FC_UNLOCK_GRP();
	return err;
}

/* Fetch data as defined by API */
int
fcomGetBlob(FcomID idnt, FcomBlobRef *pp_blob, uint32_t timeout_ms)
{
BufRef          buf;
int             rval;
#if defined(SUPPORT_SYNCGET)
struct timespec tout;
struct timeval  now;
uint32_t        usec;
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
		if ( gettimeofday(&now, 0) ) {
			return FCOM_ERR_SYS(-errno);
		}
		tout.tv_sec  = timeout_ms/1000;
		usec         = now.tv_usec + (timeout_ms - tout.tv_sec*1000)*1000;
		if ( usec > 1000000 ) {
			usec        -= 1000000;
			tout.tv_sec += 1;
		}
		tout.tv_sec += now.tv_sec;
		tout.tv_nsec = usec*1000;
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

			if ( rval ) {
				rval = ETIMEDOUT == rval ? FCOM_ERR_TIMEDOUT : FCOM_ERR_SYS(-rval);
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
			if ( FCOM_EL_NONE == ((FcomBlobRef)buf->pld)->fc_type ) {
				*pp_blob = 0;
				rval     = FCOM_ERR_NO_DATA;
			} else {
				/* increase buffer reference count before handing
				 * to user.
				 */
				fc_refb(buf);
				*pp_blob = (FcomBlobRef)buf->pld;
				rval     = 0;
			}
		} else {
			rval = FCOM_ERR_NOT_SUBSCRIBED;
		}
	__FC_UNLOCK();

	return rval;
}

/* Release blob reference as defined by API */
int
fcomReleaseBlob(FcomBlobRef *pp_blob)
{
BufRef buf;
	/* use some magic to compute the 'buf' pointer */
	buf = (BufRef)(      (uintptr_t)  (*pp_blob)
                    - (  (uintptr_t)  ((BufRef)(0))->pld
                       - (uintptr_t)   (0) ) );
	__FC_LOCK();
		fc_relb(buf);
	__FC_UNLOCK();

	*pp_blob = 0;
	return 0;
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
#ifdef SUPPORT_SYNCGET
	fprintf(f, "  failed syncget broadcasts:             %9"PRIu32"\n",
               fc_stats.bad_cond_bcst);
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

#ifdef SUPPORT_SYNCGET
		case FCOM_STAT_RX_ERR_BAD_BCST:
			v = fc_stats.bad_cond_bcst;
		break;
#endif

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
UdpCommPkt p;
uint32_t   *xmemp;
int        i,nblobs,sz,xsz;
BufRef     buf,obuf;
FcomID     idnt;

	nblobs = 0;

	/* Block for a packet */
	if ( (p = udpCommRecv(fcom_rsd, timeout_ms)) ) {

			xmemp = udpCommBufPtr(p);

			/* decode message header */
			if ( (sz = fcom_xdr_dec_msghdr(xmemp, &nblobs)) > 0 ) {

				fc_stats.n_msg++;

				for (i=0, xmemp+=sz; i < nblobs; i++) {

					fc_stats.n_blb++;

					/* extract ID and size information up-front */
					xsz = fcom_xdr_peek_size_id(&sz, &idnt, xmemp);
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
						if ( fcom_xdr_dec_blob( (FcomBlobRef)buf->pld, sz, xmemp) > 0 ) {
							__FC_LOCK();
							/* have to check again if this ID is still subscribed */
							obuf = buf;
							if ( 0 == shtblRpl(bTbl, (SHTblEntry*)&obuf, SHTBL_ADD_FAIL) ) {
								/* old entry was replaced by 'buf'; 'obuf' contains
								 * reference to old entry.
								 *
								 * Copy the subscription count and cond-var
                                 * into the new buffer.
								 */
								buf->hdr.subCnt    = obuf->hdr.subCnt;
#if defined(SUPPORT_SYNCGET)
								buf->hdr.ptr.cond  = obuf->hdr.ptr.cond;
								obuf->hdr.ptr.cond = 0;
								if ( buf->hdr.ptr.cond ) {
									/* post to blocked clients */
									if ( pthread_cond_broadcast( buf->hdr.ptr.cond ) ) {
										fc_stats.bad_cond_bcst++;
									}
								}
#endif
							}
							/* in the unlikely case that the ID had been unsubscribed
							 * since our previous check above the variable 'obuf' is
							 * still equal to 'buf' and this call hence releases the
							 * buffer we filled in vain...
							 */
							fc_relb(obuf);
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
			fprintf(stderr,"Warning (FCOM): NOT using real-time scheduler \n");
			fprintf(stderr,"                due to lack of privilege (EPERM)\n");
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
	key_off =   (uintptr_t) &((FcomBlobRef)((BufRef)0)->pld)->fc_idnt
              - (uintptr_t)  ((BufRef)0);

	if ( ! ( bTbl = shtblCreate(4 * nbufs, (unsigned long)key_off) ) ) {
		fprintf(stderr,"Fatal Error: Unable to create FCOM hash table\n");
		return FCOM_ERR_INTERNAL;
	}

	/* Start receiver */
#if defined(USE_PTHREADS) || defined(USE_EPICS)
	fc_recvr_start(fcom_rx_priority_percent);
#endif
	return 0;
}

static void fc_buf_cleanup(SHTblEntry e, void *closure)
{
	fc_relmc(FCOM_GET_GID( ((FcomBlobRef)((BufRef)e)->pld)->fc_idnt));
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
