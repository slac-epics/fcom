/* $Id: fcom_api.h,v 1.2 2009/07/12 16:00:12 strauman Exp $ */
#ifndef FCOM_API_HEADER_H
#define FCOM_API_HEADER_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * Major protocol version; changes of the
 * major version mark incompatible changes.
 * The 'fc' prefix goes only into the XDR
 * stream for some enhanced paranoia...
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
#define FCOM_SMALLVERS

#ifdef FCOM_SMALLVERS
#define FCOM_PROTO_CATCAT(maj,min) 0x##maj##min
#else
#define FCOM_PROTO_CATCAT(maj,min) 0xfc##maj##min
#endif
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

/*
 * Version 1 layout
 */
#ifdef FCOM_SMALLVERS
typedef struct FcomV1Hdr_ {
    uint8_t     vers;
	uint8_t     type;
	uint16_t    nelm;
    FcomID      idnt;  /* unique ID      */
    uint32_t    res3;  /**** reserved ****/
    uint32_t    tsHi;  /* timestamp HI   */
    uint32_t    tsLo;  /* timestamp LO   */
    uint32_t    stat;  /* status of data */
	union {
	void        *p_raw;
	float       *p_flt;
	double      *p_dbl;
	uint32_t	*p_u32;
	int32_t     *p_i32;
	int8_t      *p_i08;
	}           dref;           
	uint8_t     pad[32 - sizeof(FcomID) - 5*4 - sizeof(void*)];
} FcomV1Hdr, *FcomV1HdrRef;
#else
typedef struct FcomV1Hdr_ {
    uint32_t    vers;  /* proto vers.    */
    FcomID      idnt;  /* unique ID      */
    uint32_t    res3;  /**** reserved ****/
    uint32_t    tsHi;  /* timestamp HI   */
    uint32_t    tsLo;  /* timestamp LO   */
    uint32_t    stat;  /* status of data */
    uint32_t    type;
    uint32_t    nelm;
} FcomV1Hdr, *FcomV1HdrRef;
#endif

typedef union FcomBlobV1_ {
    FcomV1Hdr     hdr;
    struct {
        FcomV1Hdr hdr;
        uint32_t  dta[];
    }           u32;
    struct {
        FcomV1Hdr hdr;
        int32_t   dta[];
    }           i32;
    struct {
        FcomV1Hdr hdr;
        int8_t    dta[];
    }           i08;
    struct {
        FcomV1Hdr hdr;
        float     dta[];
    }           flt;
    struct {
        FcomV1Hdr hdr;
        double    dta[];
    }           dbl;
	struct {
		FcomV1Hdr hdr;
		void     *p_dta[];
	}           ptr;
} FcomBlobV1, *FcomBlobV1Ref;


typedef union FcomBlob_ {
    struct {
#ifdef FCOM_SMALLVERS
    uint8_t     vers;  /* protocol vers. */
#else
	uint32_t    vers;
#endif
    }           hdr;
    FcomBlobV1  fcb_v1;
} FcomBlob, *FcomBlobRef;

/* 
 * Helper macros to access the
 * array with the correct type.
 *
 * E.g., if you deal with a
 * 'float' array then use
 *
 *   for ( i=0; i<my_v1_blob.fc_nelm; i++ ) {
 *
 *      do_something( my_v1_blob.fc_flt[i] );
 *
 *   }
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

#ifdef FCOM_SMALLVERS
#define fc_raw    hdr.dref.p_raw
#define fc_u32    hdr.dref.p_u32
#define fc_i32    hdr.dref.p_i32
#define fc_i08    hdr.dref.p_i08
#define fc_flt    hdr.dref.p_flt
#define fc_dbl    hdr.dref.p_dbl
#else
#define fc_u32    u32.dta
#define fc_i32    i32.dta
#define fc_i08    i08.dta
#define fc_flt    flt.dta
#define fc_dbl    dbl.dta
#endif


/*
 * Helper macros to access fields
 * in a 'version 1' blob.
 *
 * E.g.,
 *
 *  FcomBlob pb;
 *
 *    pb.fc_vers     = FCOM_PROTO_VERSION_11
 *    pb.fcv1_tsHi   = my_timestamp_high;
 *    pb.fcv1_tsLo   = my_timestamp_low;
 *    pb.fcv1_idnt   = MY_ID;
 *    pb.fcv1_stat   = 0;
 *    pb.fcv1_type   = FCOM_EL_UINT32;
 *    pb.fcv1_nelm   = 1;
 *    pb.fcv1_u32[0] = my_value;
 *
 *    fcomPutBlob( &pb );
 */
#define fcv1_idnt   fcb_v1.hdr.idnt
#define fcv1_res3   fcb_v1.hdr.res3
#define fcv1_tsHi   fcb_v1.hdr.tsHi
#define fcv1_tsLo   fcb_v1.hdr.tsLo
#define fcv1_stat   fcb_v1.hdr.stat
#define fcv1_type   fcb_v1.hdr.type
#define fcv1_nelm   fcb_v1.hdr.nelm

#ifdef FCOM_SMALLVERS
#define fcv1_raw    fcb_v1.hdr.dref.p_raw
#define fcv1_u32    fcb_v1.hdr.dref.p_u32
#define fcv1_i32    fcb_v1.hdr.dref.p_i32
#define fcv1_i08    fcb_v1.hdr.dref.p_i08
#define fcv1_flt    fcb_v1.hdr.dref.p_flt
#define fcv1_dbl    fcb_v1.hdr.dref.p_dbl
#else
#define fcv1_u32    fcb_v1.u32.dta
#define fcv1_i32    fcb_v1.i32.dta
#define fcv1_i08    fcb_v1.i08.dta
#define fcv1_flt    fcb_v1.flt.dta
#define fcv1_dbl    fcb_v1.dbl.dta
#endif


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

#define FCOM_ERR_SYS(errno)     (-((errno) | (1<<16)))
#define FCOM_ERR_IS_SYS(st)     ( (st) < 0 && ((-(st)) & (1<<16)))
#define FCOM_ERR_SYS_ERRNO(st)  ( FCOM_ERR_IS_SYS(st) ? (-(st)) & 0xffff : 0 )

/* Convert error status into static string message.
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
 * In this case the group ID 
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
/* Number of failed synchronous broadcasts                 */
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
 *    FcomGroup group;
 *    FcomBlob  blob;
 *    float     data[1];
 *    int       status;
 *
 *      group = 0;
 *      // obtain a group; use any ID belonging
 *      // to the target group.
 *      if ( (status = fcomAllocGroup(XYZ_TEMP_1, &group)) )
 *        goto bail;
 *
 *      // set version
 *      blob.fc_vers     = FCOM_PROTO_VERSION_11
 *
 *      // fill-in header info
 *      getTimestamp(&blob.fcb_v1);
 *      blob.fcv1_stat   = 0;
 *      blob.fcv1_type   = FCOM_EL_FLOAT;
 *      blob.fcv1_flt    = data;
 *
 *      // fill-in data
 *      blob.fcv1_nelm   = 1;
 *      blob.fcv1_flt[0] = readADC_1();
 *
 *      // tag with ID
 *      blob.fcv1_idnt   = XYZ_TEMP_1;
 *
 *      // add to group
 *      if ( (status = fcomAddGroup(group, &blob)) )
 *        goto bail;
 *
 *      // use same version, timestamp, type, data
 *      // area and status for second blob:
 *      blob.fcv1_flt[0] = readADC_2();
 *
 *      // tag with ID
 *      blob.fcv1_id     = XYZ_PRESSURE;
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
 *
 *         fcomFreeGroup( group );
 *

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
 *        if ( p_blob->fcv1_stat ) {
 *          // handle bad status
 *        } else {
 *          // good data
 *          consumeData( p_blob->fcv1_flt[0] );
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
