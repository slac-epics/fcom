/* $Id: fcom_api.h,v 1.7 2010/03/19 19:28:14 strauman Exp $ */
#ifndef FCOM_API_HEADER_H
#define FCOM_API_HEADER_H

#include <inttypes.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * Major protocol version; changes of the
 * major version mark incompatible changes.
 *
 * Incompatible changes are:
 *   - change of the XDR encoded layout
 *   - change of the ID layout
 *   - change of min/max GID
 *   - change of the FCOM_EL_<type> 'enum' values
 *
 * An example for a compatible change would be
 *   - assignment of the reserved 'res3' field
 *     for a specific purpose.
 */

#define FCOM_PROTO_CATCAT(maj,min) 0x##maj##min
#define FCOM_PROTO_CAT(maj,min)    FCOM_PROTO_CATCAT(maj,min)

#define FCOM_PROTO_MAJ_1      1
#define FCOM_PROTO_MIN_1      1

#define FCOM_PROTO_VERSION_11 FCOM_PROTO_CAT(FCOM_PROTO_MAJ_1,FCOM_PROTO_MIN_1)
#define FCOM_PROTO_VERSION_1x FCOM_PROTO_CAT(FCOM_PROTO_MAJ_1,0)

#define FCOM_PROTO_VERSION    FCOM_PROTO_VERSION_11
#define FCOM_PROTO_MAJ        FCOM_PROTO_MAJ_1
#define FCOM_PROTO_MIN        FCOM_PROTO_MIN_1

#define FCOM_PROTO_MAJ_GET(x) ( (x) & ~0xf )
#define FCOM_PROTO_MIN_GET(x) ( (x) &  0xf )

/* Match major version */
#define FCOM_PROTO_MATCH(a,b) ( FCOM_PROTO_MAJ_GET(a) == FCOM_PROTO_MAJ_GET(b) )


/*
 * ID to identify a 'blob' of data -- NEVER make
 * assumptions about the size of this type; it may
 * change (e.g., use 'sizeof(FcomID)' and never
 * cast to/from 'uint32_t')
 */
typedef uint32_t FcomID;

/* Use this format string to print FCOM IDs */
#define FCOM_ID_FMT "0x%08"PRIx32

/*
 * ID to identify the 'group' a blob belongs to.
 */
typedef uint32_t FcomGID;

/* NOTE: any change of the min/max group
 *       numbers must trigger a change of
 *       the major protocol version since
 *       the format of the ID changes!
 */
#define FCOM_GID_MIN       8
#define FCOM_GID_MAX    2047    /* power of two - 1 */

/*
 * Special group (wildcard)
 */
#define FCOM_GID_ANY       0

#define FCOM_SID_MIN       8
#define FCOM_SID_MAX   65535    /* power of two - 1 */

#define FCOM_SID_ANY       0

/* 
 * Special ID (wildcard)
 */
#define FCOM_ID_ANY        FCOM_MAKE_ID(FCOM_GID_ANY, FCOM_SID_ANY)

/*
 * Special ID (always invalid)
 */
#define FCOM_ID_NONE       0

/*
 * Concatenate a group ID with a 'signal' id
 * to form a FcomID.
 *
 * ALWAYS use this macro; the definition
 * (e.g., version, shift count etc.) may
 * change.
 */
#define FCOM_MAKE_ID(gid,sid)   \
    ( ((FCOM_PROTO_MAJ)<<28)    \
      | ((gid)<<16)             \
      | (sid)                   \
    )


/*
 * Extract major protocol version from ID.
 * 
 *
 * NOTE: Only the 'real' major number without
 *       the 0xfc prefix is encoded in the ID.
 */
#define FCOM_GET_MAJ(id) (((id)>>28) & 0xf)

/*
 * Extract GID
 *
 * NOTE: This macro is for internal use ONLY.
 */
#define FCOM_GET_GID(id) (((id)>>16) & FCOM_GID_MAX)

/*
 * Extract SID
 *
 * NOTE: This macro is for internal use ONLY.
 */
#define FCOM_GET_SID(id) ((id) & 0xffff)

#define FCOM_GID_VALID(gid) ((gid) <= FCOM_GID_MAX && (gid) >= FCOM_GID_MIN)
#define FCOM_SID_VALID(sid) ((sid) <= FCOM_SID_MAX && (sid) >= FCOM_SID_MIN)
#define FCOM_ID_VALID(id)   (   FCOM_GID_VALID(FCOM_GET_GID(id)) \
                             && FCOM_SID_VALID(FCOM_GET_SID(id)))


/* 
 * Elementary data types;
 * ALWAYS use symbolic names when referring
 * to the type -- this may change if more
 * types are added.
 */
#define FCOM_EL_NONE    0
#define FCOM_EL_FLOAT   1
#define FCOM_EL_DOUBLE  2
#define FCOM_EL_UINT32  3
#define FCOM_EL_INT32   4
#define FCOM_EL_INT8    5
#define FCOM_EL_INVAL   6

#define FCOM_EL_TYPE(t) ((t) & 0xf)
#define FCOM_EL_SIZE(t) (  \
	FCOM_EL_FLOAT  == (t) ? sizeof(float)    : \
	FCOM_EL_DOUBLE == (t) ? sizeof(double)   : \
	FCOM_EL_UINT32 == (t) ? sizeof(uint32_t) : \
	FCOM_EL_INT32  == (t) ? sizeof(int32_t)  : \
	FCOM_EL_INT8   == (t) ? sizeof(int8_t)   : \
    -1 )

/*
 * A blob of data.
 */

typedef struct FcomBlobHdr_ {
    uint8_t     vers;  /* protocol vers. */
	uint8_t     type;  /* data el. type  */
	uint16_t    nelm;  /* # of elements  */
    FcomID      idnt;  /* unique ID      */
    uint32_t    res3;  /**** reserved ****/
    uint32_t    tsHi;  /* timestamp HI   */
    uint32_t    tsLo;  /* timestamp LO   */
    uint32_t    stat;  /* status of data */
} FcomBlobHdr, *FcomBlobHdrRef;

typedef struct FcomBlob_ {
	FcomBlobHdr hdr;
	union {
	void        *p_raw;
	float       *p_flt;
	double      *p_dbl;
	uint32_t	*p_u32;
	int32_t     *p_i32;
	int8_t      *p_i08;
	}           dref;  /* ptr to data    */
/*	uint8_t     pad[32 - sizeof(FcomID) - 5*4 - sizeof(void*)]; */
} FcomBlob, *FcomBlobRef;


/* 
 * Helper macros to access blob fields.
 *
 * THESE MACROS SHOULD ALWAYS BE USED TO ENSURE
 * COMPATIBILITY IF THE BLOB LAYOUT CHANGES.
 *
 * E.g., if you deal with a
 * 'float' array then use
 *
 *   for ( i=0; i<my_blob.fc_nelm; i++ ) {
 *
 *      do_something( my_blob.fc_flt[i] );
 *
 *   }
 *
 * E.g.,
 *
 *  FcomBlob pb;
 *  uint32_t data[10];
 *
 *    pb.fc_vers   = FCOM_PROTO_VERSION;
 *    pb.fc_tsHi   = my_timestamp_high;
 *    pb.fc_tsLo   = my_timestamp_low;
 *    pb.fc_idnt   = MY_ID;
 *    pb.fc_stat   = 0;
 *    pb.fc_type   = FCOM_EL_UINT32;
 *    pb.fc_nelm   = 1;
 *    pb.fc_u32    = data;
 *    pb.fc_u32[0] = my_value;
 *
 *    fcomPutBlob( &pb );
 *
 */
#define fc_vers   hdr.vers

#define fc_idnt   hdr.idnt
#define fc_res3   hdr.res3
#define fc_tsHi   hdr.tsHi
#define fc_tsLo   hdr.tsLo
#define fc_stat   hdr.stat
#define fc_type   hdr.type
#define fc_nelm   hdr.nelm

#define fc_raw    dref.p_raw
#define fc_u32    dref.p_u32
#define fc_i32    dref.p_i32
#define fc_i08    dref.p_i08
#define fc_flt    dref.p_flt
#define fc_dbl    dref.p_dbl


/** ERROR HANDLING ***************************************************/

/*
 * Error return values;
 */
#define FCOM_ERR_INVALID_ID     (-1)
#define FCOM_ERR_NO_SPACE       (-2)
#define FCOM_ERR_INVALID_TYPE   (-3)
#define FCOM_ERR_INVALID_COUNT  (-4)
#define FCOM_ERR_INTERNAL       (-5)
#define FCOM_ERR_NOT_SUBSCRIBED (-6)
#define FCOM_ERR_ID_NOT_FOUND   (-7)
#define FCOM_ERR_BAD_VERSION    (-8)
#define FCOM_ERR_NO_MEMORY      (-9)
#define FCOM_ERR_INVALID_ARG    (-10)
#define FCOM_ERR_NO_DATA        (-11)
#define FCOM_ERR_UNSUPP         (-12)
#define FCOM_ERR_TIMEDOUT       (-13)
#define FCOM_ERR_ID_IN_USE      (-14)

#define FCOM_ERR_SYS(errno)     (-((errno) | (1<<16)))
#define FCOM_ERR_IS_SYS(st)     ( (st) < 0 && ((-(st)) & (1<<16)))
#define FCOM_ERR_SYS_ERRNO(st)  ( FCOM_ERR_IS_SYS(st) ? (-(st)) & 0xffff : 0 )

/* Convert error status into string message.
 * System errors encoded in FCOM_ERR_SYS() are
 * converted using strerror().
 *
 * RETURNS: pointer to a non-NULL static string.
 */
const char *
fcomStrerror(int fcom_error_status);


/** INITIALIZATION ***************************************************/

#define FCOM_PORT_DEFLT         4586
/*
 * Initialize the FCOM facility.
 * 
 * ARGUMENTS:
 *
 *  mcast_g_prefix:
 *           String of the form <mcast_ip_prefix> [ : <port_no> ]
 *
 *           The <mcast_ip_prefix> is a (valid) multicast
 *           IP address that must not overlap FCOM_GID_MIN..FCOM_GID_MAX
 *           and which is used as a prefix for all transactions.
 *
 *           An optional port number may be provided (must be the
 *           same for all applications). If this is omitted then
 *           FCOM_PORT_DEFLT is used.
 *
 *  n_bufs:  Number of buffers for blobs to create. This should be a
 *           multiple of the max. # of blobs the application plans
 *           to subscribe to. If different threads 'hold' references
 *           to blobs for longer times this multiple needs to be
 *           bigger.
 *
 * RETURNS:  Zero on success, nonzero on error.
 *           
 * NOTE:     This routine is NOT thread-safe.
 */
int
fcomInit(const char *mcast_g_prefix, unsigned n_bufs);


/** GROUPS ***********************************************************/

/*
 * Opaque handle for a group.
 */
typedef void *FcomGroup;

/*
 * Obtain an empty group/container for
 * the group to which "id" belongs to.
 *
 * It is admissible to pass FCOM_ID_ANY.
 * In this case the group ID is defined
 * by the first blob with a GID different
 * from FCOM_GID_ANY.
 *
 * RETURNS: zero on success, nonzero on error.
 */
int
fcomAllocGroup(FcomID id, FcomGroup *p_group);

/*
 * Add a blob of data to a group. The data
 * are copied into the 'group' container.
 *
 * RETURNS: zero on success, nonzero on error
 *        (e.g., adding a blob with a GID part
 *        of its ID that doesn't match the GID
 *        of the group yields FCOM_ERR_INVALID_ID).
 *
 * NOTES: No transmission occurs.
 *
 *        All blobs added to a group must have
 *        the same GID part of their ID. Adding
 *        a new blob to a group that already
 *        contains one or more blobs with a 
 *        different GID results in an error.
 *
 *        It is a programming error to add more than
 *        one blob with the same ID to a group.
 *        Behavior in this case is undefined.
 *
 */

int
fcomAddGroup(FcomGroup group, FcomBlobRef p_blob);

/*
 * Discard group; release all resources.
 *
 * NOTE: Group handle must not be used anymore.
 */
void
fcomFreeGroup(FcomGroup group);


/** TRANSMISSION *****************************************************/

/*
 * Send out group.
 *
 * RETURNS: zero on success, nonzero on error.
 */
int
fcomPutGroup(FcomGroup group);

/*
 * Write a blob of data.
 * This routine may only be used for blobs
 * that are the sole members of a group!
 *
 * This demonstrates that the API becomes
 * much simpler if grouping can be avoided.
 *
 * RETURNS: zero on success, nonzero on error.
 */

int
fcomPutBlob(FcomBlobRef p_blob);


/** SUBSCRIPTION *****************************************************/

/*
 * Subscribe.
 * 
 * If the 'sync_get' argument is FCOM_SYNC_GET then
 * the subscription supports subsequent synchronous
 * 'fcomGetBlob' operations.
 *
 * This feature is optional, i.e., not enabled 
 * by default for all subscriptions because it
 * consumes additional resources.
 *
 * Also, availability of synchronous operation depends
 * on compile-time configuration.
 *
 * RETURNS: zero on success, nonzero on error.
 *          FCOM_ERR_UNSUPP if FCOM was built
 *          w/o support for synchronous operation
 *          but FCOM_SYNC_GET was specified.
 *
 */

#define FCOM_SYNC_GET  1
#define FCOM_ASYNC_GET 0
int
fcomSubscribe(FcomID id, int mode);

/*
 * Cancel subscribtion.
 *
 * NOTE: Subscription nests. Subscription is only
 *       cancelled when fcomUnsubscribe() is executed
 *       as many times (on the same ID) as fcomSubscribe()
 *       had been.
 *
 * RETURNS: zero on success, nonzero on error.
 *       The last, unnesting, fcomUnsubscribe() operation
 *       may fail when attempting to unsubscribe an 'id' on
 *       which another thread is currently blocking
 *       (synchronous fcomGetBlob()).
 */
int
fcomUnsubscribe(FcomID id);


/** RECEPTION ********************************************************/

/*
 * Obtain a pointer to a blob of data from the cache.
 *
 * If the 'timeout_ms' argument is zero then an ordinary,
 * asynchronous operation is performed. If 'timeout_ms' is
 * nonzero then the calling thread is blocked for at most
 * 'timeout_ms' milliseconds or until fresh data arrive.
 * Synchronous operation is only possible if the a subscription
 * to 'id' with the FCOM_SYNC_GET attribute had been performed.
 * Also, availability of this feature depends on compile-time
 * configuration of FCOM.
 * 
 * RETURNS: zero on success, nonzero on error.
 *          Pointer to retrieved blob is returned in *pp_blob.
 *          In particular, FCOM_ERR_NO_DATA may be returned if
 *          no data have arrived since subscription.
 *          FCOM_ERR_UNSUPP is returned if 'timeout_ms' is
 *          nonzero but FCOM was built w/o support for
 *          synchronous operation.
 *          FCOM_ERR_NOT_SUBSCRIBED is returned if either no
 *          subscription for 'id' exists or a blocking/
 *          synchronous operation is attempted but no
 *          subscription with the FCOM_SYNC_GET attribute exists.
 *
 * NOTES:   User must not modify/write the retrieved blob.
 *
 *          User must release the blob when done (fcomReleaseBlob()
 *          below).
 *
 *          The retrieved blob is NOT overwritten or updated
 *          when fresh data arrive.
 */

int
fcomGetBlob(FcomID id, FcomBlobRef *pp_blob, uint32_t timeout_ms);

/*
 * Release reference to a blob of data. If
 * the reference count drops to zero then
 * FCOM releases resources associated with the
 * blob.
 *
 * RETURNS: zero on success, nonzero on error.
 *
 *          NULL is stored into *pp_blob (on success).
 */
int
fcomReleaseBlob(FcomBlobRef *pp_blob);

/*
 * BLOB SETS
 *
 * Sets of blobs allow an application to block until
 * either any or all of the listed blobs arrive.
 */

typedef uint32_t FcomBlobSetMask;

/* Opaque type; for FCOM internal use only */
typedef struct FcomBlobSetHdr  *FcomBlobSetHdrRef;

typedef struct FcomBlobSetMemb *FcomBlobSetMembRef;

typedef struct FcomBlobSetMemb {
	FcomID			idnt;
	FcomBlobRef		blob;
	FcomBlobSetHdrRef	head; /* for INTERNAL USE ONLY */
	FcomBlobSetMembRef      next; /* for INTERNAL USE ONLY */
} FcomBlobSetMemb;

typedef struct FcomBlobSet_ {
	unsigned            nmemb;
	FcomBlobSetMemb     memb[];
} FcomBlobSet, *FcomBlobSetRef;

/* Allocate a set of blobs. You must pass a list of IDs and will obtain
 * a set with all memb[i].blob references == NULL.
 * All IDs must previously have been subscribed and none of them
 * can be unsubscribed (i.e. the nest count cannot drop to zero)
 * while being member of a set.
 *
 * After use, the set can be destroyed with fcomFreeBlobSet().
 *
 * RETURNS: Zero on success error status on failure. A valid set reference
 *          is only returned in *pp_set if the call is successful.
 *
 * NOTES:   Sets are 'single threaded', i.e., it is a programming error
 *          if multiple threads pass the same set to fcomGetBlobSet()
 *          simultaneously.
 *          It is also a programming error to free a set while a
 *          fcomGetBlobSet() operation is in progress.
 */
int
fcomAllocBlobSet(FcomID member_id[], unsigned num_members, FcomBlobSetRef *pp_set);

/* Destroy a blob set. Any remaining (non-NULL) blob references memb[i].blob are
 * automatically released.
 *
 * RETURNS: zero on success, nonzero (status) on error.
 */
int
fcomFreeBlobSet(FcomBlobSetRef p_set);

/*
 * Wait for a set of blobs to arrive.
 * 
 * The 'waitfor' and '*p_res' arguments are bitmasks with bit (1<<i) corresponding
 * to set member 'i' in p_set->memb[i].
 * 
 * If the FCOM_SET_WAIT_ANY flag is passed then the call returns if any of the members
 * with its corresponding bit in 'waitfor' is updated.
 * If FCOM_SET_WAIT_ALL is passed then the call only returns when all members with
 * their bits set in 'waitfor' are updated (or the timeout expires).
 *
 * All members that were updated while waiting will have their bit set in *p_res
 * upon return.
 *
 * RETURNS: zero on success or nonzero failure status.
 *
 * NOTES:   Even if the timeout expires (FCOM_ERR_TIMEDOUT) some members still
 *          may have been updated (with their bit set in p_res).
 *
 *          It is legal to repeatedly call fcomGetBlobSet() with the same 'p_set'
 *          reference. Any non-null blob references that are 'attached' to the set
 *          are automatically released.
 *
 *          If the user wants to 'preserve' a blob reference then the corresponding
 *          memb[i].blob pointer must be set to NULL but it is then the user's
 *          responsibility to execute fcomReleaseBlob() explicitly.
 *
 *          Only blobs that were requested in the 'waitfor' mask are updated.
 *        
 */
#define FCOM_SET_WAIT_ANY	0
#define FCOM_SET_WAIT_ALL	1

int
fcomGetBlobSet(FcomBlobSetRef p_set, FcomBlobSetMask *p_res,  FcomBlobSetMask waitfor, int flags, uint32_t timeout_ms);


/** STATISTICS *******************************************************/

/*
 * Dump statistics to a FILE. If a NULL
 * file pointer is passed then 'stdout'
 * is used.
 */
void
fcomDumpStats(FILE *f);

/*
 * Obtain statistics information.
 * 
 * Note that most if not all counters are internally only 32-bits.
 * The 64-bit exchange data type is used for future enhancements.
 *
 * RETURNS: 0 on success, FCOM_ERR_UNSUPP when asking for an
 *          unknown key.
 *
 * NOTE:    There might be no locking implemented, i.e., values
 *          belonging to two different keys might be inconsistent;
 *          it is assumed that 32-bit quantities can be accessed
 *          atomically by the CPU provided that they are properly
 *          aligned.
 *          This facility is intended for informational/diagnostic
 *          purposes only.
 */
int
fcomGetStats(int n_keys, uint32_t key_arr[], uint64_t value_arr[]);

#define FCOM_RX_32_STAT(n) ((FCOM_PROTO_MAJ_1<<28)|(1<<24)|((n)<<16))
#define FCOM_TX_32_STAT(n) ((FCOM_PROTO_MAJ_1<<28)|(2<<24)|((n)<<16))

/* Test if a given key gives 32 or 64-bit values           */
#define FCOM_STAT_IS_32(key)   (0 == ((key) & (4<<24)))
#define FCOM_STAT_IS_64(key)   (0 != ((key) & (4<<24)))

/* These macros are for internal use only                  */
#define FCOM_STAT_IS_RX(key)   (1 == (((key)>>24) & 3))
#define FCOM_STAT_IS_TX(key)   (2 == (((key)>>24) & 3))
#define FCOM_STAT_IS_V1(key)   (FCOM_PROTO_MAJ_1 == (((key)>>28) & 0xf))
#define FCOM_STAT_KIND(key)    ((key)&0xffff)


/* Keys for RX statistics         */

/* Number of blobs received                                */
#define FCOM_STAT_RX_NUM_BLOBS_RECV       FCOM_RX_32_STAT(1)
/* Number of messages/groups received                      */
#define FCOM_STAT_RX_NUM_MESGS_RECV       FCOM_RX_32_STAT(2)
/* Failed attempts to allocate buffer (lack of buffers)    */
#define FCOM_STAT_RX_ERR_NOBUF            FCOM_RX_32_STAT(3)
/* XDR decoder errors                                      */
#define FCOM_STAT_RX_ERR_XDRDEC           FCOM_RX_32_STAT(4)
/* Number of blobs with bad/unknown version received       */
#define FCOM_STAT_RX_ERR_BAD_BVERS        FCOM_RX_32_STAT(5)
/* Number of msgs/groups with bad/unknown version received */
#define FCOM_STAT_RX_ERR_BAD_MVERS        FCOM_RX_32_STAT(6)
/* Number of failed synchronous or set member broadcasts   */
#define FCOM_STAT_RX_ERR_BAD_BCST         FCOM_RX_32_STAT(7)
/* Number of subscribed blobs                              */
#define FCOM_STAT_RX_NUM_BLOBS_SUBS       FCOM_RX_32_STAT(8)
/* Max. supported number of subscribed blobs               */
#define FCOM_STAT_RX_NUM_BLOBS_MAX        FCOM_RX_32_STAT(9)

/* Keys for RX buffer statistics  */

/* Number of different kinds (sizes) of buffers supported  */
#define FCOM_STAT_RX_NUM_BUF_KINDS        FCOM_RX_32_STAT(10)
/* Separate statistics/properties for each buffer kind     */
#define FCOM_STAT_RX_BUF_SIZE(kind)       (FCOM_RX_32_STAT(11) | FCOM_STAT_KIND(kind))
/* Total number of buffers of a particular kind/size       */
#define FCOM_STAT_RX_BUF_NUM_TOT(kind)    (FCOM_RX_32_STAT(12) | FCOM_STAT_KIND(kind))
/* Number of available/free buffers of a particular kind   */
#define FCOM_STAT_RX_BUF_NUM_AVL(kind)    (FCOM_RX_32_STAT(13) | FCOM_STAT_KIND(kind))
/* Guaranteed alignment of payload of a buffer kind        */
#define FCOM_STAT_RX_BUF_ALIGNED(kind)    (FCOM_RX_32_STAT(14) | FCOM_STAT_KIND(kind))

/* Keys for TX statistics         */

/* Number of blobs sent                                    */
#define FCOM_STAT_TX_NUM_BLOBS_SENT       FCOM_TX_32_STAT(1)
/* Number of messages/groups sent                          */
#define FCOM_STAT_TX_NUM_MESGS_SENT       FCOM_TX_32_STAT(2)
/* Number of failed attempts to send (TCP/IP stack errors) */
#define FCOM_STAT_TX_ERR_SEND             FCOM_TX_32_STAT(3)

/* Dump statistics and contents of a given ID to 'f'
 * (stdout if NULL).
 *
 * If 'level' is nonzero then more verbose information is
 * printed (including payload data).
 *
 * RETURNS: Number of characters printed or (negative) error status.
 */
int
fcomDumpIDStats(FcomID idnt, int level, FILE *f);

/* Like fcomDumpIDStats but dump info about a given blob
 *
 * RETURNS: Number of characters printed or (negative) error status.
 */
int
fcomDumpBlob(FcomBlobRef blob, int level, FILE *f);


/** EXAMPLES *********************************************************/

/*
 * Example for the use of FCOM:
 *
 * A] Header files defining IDs
 *
 * A.1] Global Header <groups.h> Defining Group/IOC IDs
 *
 *    #include <fcom_api.h>
 *
 *    // Define group IDs; one for each IOC and one
 *    // for each controller:
 *
 *    #define GID_IOC_ABC       (FCOM_GID_MIN + 0)
 *    #define GID_IOC_DEF       (FCOM_GID_MIN + 1)
 *    #define GID_IOC_XYZ       (FCOM_GID_MIN + 2)
 *
 *    #define GID_LOOP_1        (FCOM_GID_MIN + 2)
 *
 *    // Define macro to assemble the signal part
 *    // of an ID from subsystem/IOC sub-part and
 *    // "PV" part defined by subsystem designer.
 *
 *    #define MAKE_SID(sys,sig)    (((sys)<<6)|(sig))
 *
 * A.2] Detector (XYZ) IOC specific header <detector_xyz.h>:
 *
 *    #include <groups.h>
 *
 *    // ID Definitions for detector IOC.
 *    // Detector IOC subsystem designer
 *    // defines this:
 *
 *    // Two signals acquired by IOC XYZ
 *    #define SIG_XYZ_TEMP_1    MAKE_SID(GID_IOC_XYZ, 1)
 *    #define SIG_XYZ_PRESSURE  MAKE_SID(GID_IOC_XYZ, 2)
 *
 *    // Define complete IDs including group.
 *    // Group of detector signals is detector IOC GID:
 *    #define XYZ_TEMP_1        FCOM_MAKE_ID(GID_IOC_XYZ, SIG_XYZ_TEMP_1)
 *    #define XYZ_PRESSURE      FCOM_MAKE_ID(GID_IOC_XYZ, SIG_XYZ_PRESSURE)
 *

 * A.3] Actuator IOC (ABC) specific definitions
 *
 *    #include <groups.h>
 *
 *    // Actuator signals residing on ABC:
 *    #define SIG_ABC_CURR_1    MAKE_SID(GID_IOC_ABC, 1)
 *
 * A.4] Actuator IOC (DEF) specific definitions
 *
 *    #include <groups.h>
 *
 *    // Actuator signals residing on DEF:
 *    #define SIG_DEF_CURR_2    MAKE_SID(GID_IOC_DEF, 1)
 *
 * A.5] Definitions for loop controller 1
 *
 *    #include <groups.h>
 *    #include <actuator_abc.h>
 *    #include <actuator_def.h>
 *
 *    // Group of actuator signals is GID
 *    // of controller/loop:
 *    #define LOOP_1_CURR_1     FCOM_MAKE_ID(GID_LOOP_1, SIG_ABC_CURR_1)
 *    #define LOOP_1_CURR_2     FCOM_MAKE_ID(GID_LOOP_1, SIG_DEF_CURR_2)
 *

 * B] Data acquisition system sends two blobs of data
 *
 *    #include <fcom_api.h>
 *    #include <detector_xyz.h>
 *
 *    FcomGroup group = 0;
 *    FcomBlob  blob;
 *    float     data[1];
 *    int       status;
 *
 *      // obtain a group; use any ID belonging
 *      // to the target group.
 *      if ( (status = fcomAllocGroup(XYZ_TEMP_1, &group)) )
 *        goto bail;
 *
 *      // fill-in header info
 *      blob.fc_vers     = FCOM_PROTO_VERSION;
 *      getTimestamp(&blob);
 *      blob.fc_stat     = 0;
 *      blob.fc_type     = FCOM_EL_FLOAT;
 *      blob.fc_flt      = data;
 *
 *      // fill-in data
 *      blob.fc_nelm     = 1;
 *      blob.fc_flt[0]   = readADC_1();
 *
 *      // tag with ID
 *      blob.fc_idnt     = XYZ_TEMP_1;
 *
 *      // add to group
 *      if ( (status = fcomAddGroup(group, &blob)) )
 *        goto bail;
 *
 *      // use same version, timestamp, type, data
 *      // area and status for second blob:
 *      blob.fc_flt[0]   = readADC_2();
 *
 *      // tag with ID
 *      blob.fc_id       = XYZ_PRESSURE;
 *
 *      // add to group
 *      if ( (status = fcomAddGroup(group, &blob)) )
 *        goto bail;
 *
 *      // done; send off
 *      status = fcomPutGroup(group);
 * 
 *      // group is now gone, even if sending failed
 *      group = 0;
 *
 *      bail:
 *         // print message if there was an error
 *         if ( status )
 *           fprintf(stderr,"FCOM error: %s\n", fcomStrerror(status));
 *         fcomFreeGroup( group );

 * C] Receiver subscribes to XYZ_TEMP_1
 *
 *      #include <fcom_api.h>
 *      #include <detector_xyz.h>
 *
 *      // during initialization
 *      fcomSubscribe(XYZ_TEMP_1, FCOM_ASYNC_GET);
 *
 *      ...
 *
 *      FcomBlobRef p_blob;
 *
 *      // normal execution; get data from cache
 *      if ( 0 == fcomGetBlob( XYZ_TEMP_1, &p_blob, 0 ) ) {
 *        // access data; assume we know the version, type and 
 *        // count but could verify...
 *        if ( p_blob->fc_stat ) {
 *          // handle bad status
 *        } else {
 *          // good data
 *          consumeData( p_blob->fc_flt[0] );
 *        }
 *        // done -- release blob
 *        fcomReleaseBlob( &p_blob );
 *      } else {
 *        // error handling
 *      }
 *
 *      // if XYZ_TEMP_1 is never needed again we may
 *      // unsubscribe.
 *      fcomUnsubscribe( XYZ_TEMP_1 );
 *

 * D] Get statistics; find out how many available buffers of
 *    size >= 512 bytes there are:
 *
 *    Obtain number of different buffer kinds:
 *
 *     uint32_t key = FCOM_STAT_RX_NUM_BUF_KINDS;
 *     uint64_t nkinds;
 *     fcomGetStats(1, &key, &nkinds);
 *
 *    Obtain size and number of free buffers for all kinds.
 *
 *     uint32_t *keys = malloc(sizeof(uint32_t)*2*nkinds);
 *     uint64_t *vals = malloc(sizeof(uint64_t)*2*nkinds);
 *
 *    Generate keys for all kinds of buffers
 *     for ( i=0; i<nkinds; i++ ) {
 *       keys[i]        = FCOM_STAT_RX_BUF_SIZE(i);
 *       keys[i+nkinds] = FCOM_STAT_RX_BUF_NUM_AVL(i); 
 *     }
 *    
 *    Get stats
 *     fcomGetStats(nkinds*2, keys, vals);
 *
 *    Count available buffers >= 512
 *
 *    unsigned count = 0;
 *    for ( i=0; i<nkinds; i++ ) {
 *      if ( vals[i] >= 512 )
 *        count += vals[i+nkinds];
 *      }
 *    }
 *   
 */

#ifdef __cplusplus
}
#endif

#endif
