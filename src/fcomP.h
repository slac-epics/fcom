/* $Id$ */
#ifndef FCOM_PRIVATE_API_H
#define FCOM_PRIVATE_API_H

/* FCOM internal API.
 *
 * Author: Till Straumann <strauman@slac.stanford.edu>, 2009.
 *
 * These routines MUST NOT be used directly by applications.
 *
 ******************************************************
 * This API may change in incompatible ways w/o notice.
 ******************************************************
 *
 * Non-public routines and global objects have a
 * 'fcom_' prefix and use all-lowercase names.
 *
 * The public API routines use a 'fcom' prefix and
 * mixed-case names.
 */

#include <stdio.h>
#include <fcom_api.h>
#include <udpComm.h>

/* We align all data to 16-bytes (just in case someone wants to
 * vectorize access to FCOM data)
 */
#define FC_ALIGNMENT  16	 /* must be power of two */
#define FC_ALIGN_MSK  (FC_ALIGNMENT - 1)
#define FC_ALIGN(ptr) ((((uintptr_t)(ptr)) + FC_ALIGN_MSK) & ~((uintptr_t)FC_ALIGN_MSK))


/* Mcast group prefix in network byte order */
extern uint32_t fcom_g_prefix;

/* Our port */
extern int      fcom_port;

/* Our socket (transmission) */
extern int      fcom_xsd;
/* Our socket (reception)    */
extern int      fcom_rsd;

/* RX thread priority (pthread) */
extern int      fcom_rx_priority_percent;

/* Clean up and terminate FCOM (undocumented; for testing only) */
int
fcom_exit(void);

static __inline__
int fcom_get_gid(FcomBlobRef pb, uint32_t *p_gid)
{
	switch ( FCOM_PROTO_MAJ_GET(pb->fc_vers) ) {
		case FCOM_PROTO_VERSION_1x:
			*p_gid = FCOM_GET_GID(pb->fcb_v1.fc_idnt);
		return 0;

		default:
		break;
	}
	return -1;
}

/* Find 1-based position of most non-zero bit in x.
 * E.g., fcom_nzbits(0x15) -> 5.
 */
int
fcom_nzbits(uint32_t x);

extern int fcom_recv_fini()               __attribute__((weak));
extern int fcom_recv_init(unsigned nbufs) __attribute__((weak));
extern int fcom_send_fini()               __attribute__((weak));
extern int fcom_send_init()               __attribute__((weak));

/* Block (for at most timeout_ms milliseconds)
 * for a single message to arrive. Dispatch the
 * blobs contained in the message to the internal
 * hash table to be picked up by fcomGetBlob().
 *
 * RETURNS: # of blobs received (0 if timeout)
 */
int
fcom_receive(unsigned timeout_ms);

/* Dump FCOM RX statistics to file 'f'.
 * 'stdout' is used if f = NULL.
 */
extern void
fcom_recv_stats(FILE *f)                  __attribute__((weak));

/* Dump FCOM TX statistics to file 'f'.
 * 'stdout' is used if f = NULL.
 */
extern void
fcom_send_stats(FILE *f)                  __attribute__((weak));

/* Read/write a ASCII file (stdio) defining a sequence of
 * blobs and convert to/from C-representation.
 *
 * This code is intended FOR TESTING PURPOSES.
 * The ASCII file format is
 *  file: { record } eof
 *
 *  eof:
 *    'EF 0'
 *
 *  record: header , data , eor
 *
 *  eor:
 *    'ER 0'
 * 
 *  header:
 *    've' , number
 *    'id' , number
 *    'th' , number
 *    'tl' , number
 *    'st' , number
 *    'ty' , type , count
 *
 *  type:  '1'..'5' 
 *
 *  (representing FCOM_EL_FLT .. FCOM_EL_UINT8)
 *
 *  count: number
 *
 *  data:  <"count" lines of numbers of "type">
 *
 *  E.g., to construct a blob of version 0xfc12, id 0x1004001a
 *  holding 3 'double' numbers (type == 2) we say:
 *
 *  ve 0x0000fc12
 *  id 0x1004001a
 *  th 0x00000000    [ timestamp HI -> 0 ]
 *  tl 0x00000000    [ timestamp LO -> 0 ]
 *  st 0x00000000    [ status       -> 0 ]
 *  ty 0x00000002  3
 *       2.456
 *       5.8891E4
 *       33.0
 *  ER 0
 *  EF 0
 */

/* Scan a blob in ASCII representation from a file 'f' and store
 * into *pb. Pass the available amount of memory for storage (in
 * bytes) in 'avail'.
 * 
 * RETURNS: - number of bytes stored into *pb         OR
 *          - zero if 'EF 0' (eof) marker was reached OR
 *          - value < 0 on error.
 */

int
fcom_get_blob_from_file(FILE *f, FcomBlobV1Ref pb, int avail);

/* Write a blob in ASCII representation to a file 'f'.
 * 
 * RETURNS: zero on success, nonzero on error.
 * 
 * NOTE:    terminating 'EF 0' (eof) marker must be
 *          appended by caller.
 */

int
fcom_put_blob_to_file(FILE *f, FcomBlobV1Ref pb);

/* Get RX and TX statistic, respectively. */
extern int
fcom_get_rx_stat(uint32_t key, uint64_t *p_val)
__attribute__((weak));

extern int
fcom_get_tx_stat(uint32_t key, uint64_t *p_val)
__attribute__((weak));

/* Add more buffers of a given kind at run-time
 * This routine is thread safe.
 *
 * RETURNS: zero on error, nonzero on failure.
 */
int
fcom_add_bufs(unsigned kind, unsigned num_bufs);

#endif
