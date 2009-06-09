#ifndef FCOM_API_HEADER_H
#define FCOM_API_HEADER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/*
 * Special group (wildcard)
 */
#define FCOM_GID_ANY       0

#define FCOM_GID_MIN       8
#define FCOM_GID_MAX    2047

#define FCOM_SID_MIN       8
#define FCOM_SID_MAX   65535

/*
 * Concatenate a group ID with a 'signal' id
 * to form a FcomID.
 *
 * ALWAYS use this macro; the definition
 * (e.g., shift count etc.) may change.
 */
#define FCOM_MAKE_ID(gid,sid)    (((gid)<<16)|(sid))

/* 
 * Elementary data types;
 * ALWAYS use symbolic names when referring
 * to the type -- this may change if more
 * types are added.
 */
#define FCOM_NONE       0
#define FCOM_EL_UINT32  1
#define FCOM_EL_INT32   2
#define FCOM_EL_FLOAT   3
#define FCOM_EL_DOUBLE  4


typedef struct FcomUint32_ {
    uint32_t    count;
    uint32_t    data[];
} FcomUint32;

typedef struct FcomInt32_ {
    uint32_t    count;
    int32_t     data[];
} FcomInt32;

typedef struct FcomFloat_ {
    uint32_t    count;
    float       data[];
} FcomFloat;

typedef struct FcomDouble_ {
    uint32_t    count;
    double      data[];
} FcomDouble;

/*
 * A blob of data.
 */
typedef struct FcomBlob_ {
    FcomID        fc_id;  /* unique ID      */
    uint32_t    fc_res1;  /**** reserved ****/
    uint32_t    fc_tsHi;  /* timestamp HI   */
    uint32_t    fc_tsLo;  /* timestamp LO   */
    uint32_t    fc_stat;  /* status of data */
    uint32_t    fc_type;  /* data type      */
    uint32_t    fc_res2;  /**** reserved ****/
    union {
        uint32_t            count;
        struct FcomUint32_  u32;
        struct FcomInt32_   i32;
        struct FcomFloat_   flt;
        struct FcomDouble_  dbl;
    }           fc_data;
} FcomBlob, *FcomBlobRef;

/* 
 * Helper macros to access the
 * array with the correct type.
 *
 * E.g., if you deal with a
 * 'float' array then use
 *
 *   for ( i=0; i<my_blob.fc_count; i++ ) {
 *
 *      do_something( my_blob.fc_flt[i] );
 *
 *   }
 *
 */
#define fc_count  fc_data.count
#define fc_u32    fc_data.u32.data
#define fc_i32    fc_data.i32.data
#define fc_flt    fc_data.flt.data
#define fc_dbl    fc_data.dbl.data

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

/*
 * Opaque handle for a group.
 */
typedef void *FcomGroup;

/*
 * Obtain an empty group/container for
 * the group to which "id" belongs to.
 *
 * RETURNS: zero on success, nonzero on error.
 */
int
fcomAllocGroup(FcomID id, FcomGroup *p_group);

/*
 * Add a blob of data to a group. The data
 * are copied into the 'group' container.
 *
 * RETURNS: zero on success, nonzero on error.
 *
 * NOTES: No transmission occurs.
 *
 *        Group ID part of blob->fc_id may be FCOM_GID_ANY.
 *
 *        It is a programming error to add more than
 *        one blob with the same ID to a group.
 *        Behavior in this case is undefined.
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


/*
 * Subscribe.
 *
 * RETURNS: zero on success, nonzero on error.
 *
 */

int
fcomSubscribe(FcomID id);

/*
 * Cancel subscribtion.
 *
 * NOTE: Subscription nests. Subscription is only
 *       cancelled when fcomUnsubscribe() is executed
 *       as many times (on the same ID) as fcomSubscribe()
 *       had been.
 *
 * RETURNS: zero on success, nonzero on error.
 */
int
fcomUnsubscribe(FcomID id);

/*
 * Obtain a pointer to a blob of data from the cache.
 * 
 * RETURNS: zero on success, nonzero on error.
 *        Pointer to retrieved blob is returned in
 *        *pp_blob.
 *
 * NOTES: User must not modify/write the retrieved
 *        blob.
 *
 *        User must release the blob when done
 *        (fcomReleaseBlob() below).
 *
 *        The retrieved blob is NOT overwritten or
 *        updated when fresh data arrive.
 */

int
fcomGetBlob(FcomID id, FcomBlobRef *pp_blob);

/*
 * Release reference to a blob of data. If
 * the reference count drops to zero then
 * FCOM releases resources associated with the
 * blob.
 *
 * RETURNS: zero on success, nonzero on error.
 *
 *          NULL is stored into *pp_blob.
 */
int
fcomReleaseBlob(FcomBlobRef *pp_blob);


/*
 * Example for the use of FCOM:
 *
 * A] Header files defining IDs
 *
 * A.1] Global Header Defining Group/IOC IDs
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
 * A.2] Detector (XYZ) IOC specific header:
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
 *
 *      // obtain a group; use any ID belonging
 *      // to the target group.
 *      fcomAllocGroup(XYZ_TEMP_1, &group);
 *
 *      // fill-in header info
 *      getTimestamp(&blob);
 *      blob.fc_stat   = 0;
 *      blob.fc_type   = FCOM_EL_FLOAT;
 *
 *      // fill-in data
 *      blob.fc_count  = 1;
 *      blob.fc_flt[0] = readADC_1();
 *
 *      // tag with ID
 *      blob.fc_id     = XYZ_TEMP_1;
 *
 *      // add to group
 *      fcomAddGroup(group, &blob);
 *
 *      // use same timestamp, type and status
 *      // for second blob:
 *      blob.fc_flt[0] = readADC_2();
 *
 *      // tag with ID
 *      blob.fc_id     = XYZ_PRESSURE;
 *
 *      // add to group
 *      fcomAddGroup(group, &blob);
 *
 *      // done; send off
 *      fcomPutGroup(group);
 *
 * C] Receiver subscribes to XYZ_TEMP_1
 *
 *      #include <fcom_api.h>
 *      #include <detector_xyz.h>
 *
 *      // during initialization
 *      fcomSubscribe(XYZ_TEMP_1);
 *
 *      ...
 *
 *      FcomBlobRef p_blob;
 *
 *      // normal execution; get data from cache
 *      if ( 0 == fcomGetBlob( XYZ_TEMP_1, &p_blob ) ) {
 *        // access data; assume we know the type and 
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
 */

#ifdef __cplusplus
}
#endif

#endif
