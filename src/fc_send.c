/* $Id: fc_send.c,v 1.4 2010/01/13 18:29:20 strauman Exp $ */

/* 
 * Implementation of the FCOM sender's high-level parts.
 *
 * Author: Till Straumann <strauman@slac.stanford.edu>, 2009.
 */

#define __INSIDE_FCOM__
#include <fcom_api.h>
#include <fcomP.h>
#include <udpComm.h>
#include <xdr_dec.h>
#include <stdio.h>
#include <inttypes.h>

#include <netinet/in.h> /* for htonl & friends only */

#ifdef ENABLE_PROFILE
#include <sys/time.h>
#endif

static struct {
	uint32_t n_msg;
	uint32_t n_blb;
	uint32_t n_snderr;
} fc_stats = {0};

/* We directly use udpComm packets to hold FCOM
 * groups. The low-level xdr-encoder stores
 * internal state information in the first two 32-bit
 * words which eventually hold the XDR encoded version
 * number and blob-count, respectively (see xdr_enc.c).
 */

int
fcomAllocGroup(FcomID id, FcomGroup *p_grp)
{
UdpCommPkt grp;
int        rval;
uint32_t   *xmem;

	if ( FCOM_GET_MAJ(id) != FCOM_PROTO_MAJ_1 )
		return FCOM_ERR_BAD_VERSION;

	if ( ! (grp = udpCommAllocPacket()) ) {
		return FCOM_ERR_NO_MEMORY;
	}

	if ( ((uintptr_t)grp) & (sizeof(uint32_t)-1) ) {
		rval = FCOM_ERR_INTERNAL;
		goto bail;
	}

	xmem = udpCommBufPtr(grp);

	if ( (rval = fcom_msg_init(xmem, UDPCOMM_PKTSZ, FCOM_GET_GID(id))) < 0 )
		goto bail;

	rval   = 0;
	*p_grp = (FcomGroup)grp;
	grp    = 0;

bail:
	if ( grp )
		udpCommFreePacket(grp);
	return rval;
}

int
fcomAddGroup(FcomGroup grp, FcomBlobRef pb)
{
int      rval;
uint32_t *xmem;

	xmem = udpCommBufPtr((UdpCommPkt)grp);
	rval = fcom_msg_append_blob(xmem, pb);

	return rval < 0 ? rval : 0;
}

void
fcomFreeGroup(FcomGroup grp)
{
	udpCommFreePacket((UdpCommPkt)grp);
}

static int
sendtogid(UdpCommPkt p, uint32_t len, uint32_t gid)
{
int      rval;
uint32_t dip;

	/* Form destination IP address */
	dip = fcom_g_prefix | htonl(gid);
		
	rval = udpCommSendPktTo(fcom_xsd, p, len, dip, fcom_port);

	if ( rval < 0 ) {
		rval = FCOM_ERR_SYS(-rval);
		fc_stats.n_snderr++;
	} else {
		rval = 0;
		fc_stats.n_msg++;
	}

	return rval;
}

#ifdef ENABLE_PROFILE
static __inline__ uint32_t 
tvdiff(struct timeval *pa, struct timeval *pb)
{
uint32_t diff=0;
	if ( pa->tv_usec < pb->tv_usec )
		diff += 1000000L;
	/* assume always < 1s */
	diff += pa->tv_usec - pb->tv_usec;
	return diff;
}

uint32_t tx_prof[20] = {0};
int      tx_prdx     = 0;
#define  ADDPROF(i, a, b)              \
	do {                               \
		gettimeofday( &a, 0 );         \
		tx_prof[i++] = tvdiff(&a, &b); \
		b = a;                         \
	} while (0)
#else
#define ADDPROF(i, a, b) do { } while (0)
#endif

int
fcomPutBlob(FcomBlobRef pb)
{
UdpCommPkt     p;
uint32_t       *xmem;
int            rval;
uint32_t       gid;
#ifdef ENABLE_PROFILE
struct timeval ta, tb;
#endif


	if ( FCOM_PROTO_MAJ_GET(pb->fc_vers) != FCOM_PROTO_VERSION_1x )
		return FCOM_ERR_BAD_VERSION;

#ifdef ENABLE_PROFILE
	gettimeofday( &tb, 0 );
	tx_prdx = 0;
#endif

	if ( ! (p = udpCommAllocPacket()) ) {
		return FCOM_ERR_NO_MEMORY;
	}

	ADDPROF(tx_prdx, ta, tb);

	xmem = udpCommBufPtr(p);

	if ( (rval = fcom_msg_one_blob(xmem, UDPCOMM_PKTSZ, pb, &gid)) < 0 ) {
		udpCommFreePacket(p);
		return rval;
	}

	ADDPROF(tx_prdx, ta, tb);

	if ( ! FCOM_GID_VALID( gid ) ) {
		udpCommFreePacket(p);
		return FCOM_ERR_INVALID_ID;
	}

	rval = sendtogid(p, rval * sizeof(uint32_t), gid);

	ADDPROF(tx_prdx, ta, tb);

	if ( 0 == rval )
		fc_stats.n_blb++;

	return rval;
}

int
fcomPutGroup(FcomGroup group)
{
uint32_t nblobs;
uint32_t gid;
uint32_t nints;
uint32_t *xmem;
int      rval;

	xmem   = udpCommBufPtr((UdpCommPkt)group);

	/*
	 * GID and total number of 32-bit words
     * stored in 'xmem' are returned by fcom_msg_end().
     */
	nints = fcom_msg_end(xmem, &gid, &nblobs);

	if ( ! FCOM_GID_VALID(gid) ) {
		fcomFreeGroup(group);	
		return FCOM_ERR_INVALID_ID;
	}

	rval = sendtogid((UdpCommPkt)group, nints * sizeof(uint32_t), gid);
	
	if ( 0 == rval )
		fc_stats.n_blb += nblobs;

	return rval;
}

/*
 * provide dummy initializer/finalizer for the initialization
 * code to know that the TX stuff was linked.
 */

int
fcom_send_init()
{
	return 0;
}

int
fcom_send_fini()
{
	return 0;
}

void
fcom_send_stats(FILE *f)
{
	if ( !f )
		f = stdout;
	fprintf(f, "FCOM Tx Statistics:\n");
	fprintf(f, "  messages sent: %4"PRIu32"\n", fc_stats.n_msg);
	fprintf(f, "  blobs sent:    %4"PRIu32"\n", fc_stats.n_blb);
	fprintf(f, "  send errors:   %4"PRIu32"\n", fc_stats.n_snderr);
}

int
fcom_get_tx_stat(uint32_t key, uint64_t *p_val)
{
uint32_t v;
		switch ( key ) {
			case FCOM_STAT_TX_NUM_BLOBS_SENT:
				v = fc_stats.n_blb;
			break;

			case FCOM_STAT_TX_NUM_MESGS_SENT:
				v = fc_stats.n_msg;
			break;

			case FCOM_STAT_TX_ERR_SEND:
				v = fc_stats.n_snderr;
			break;

			default:
			return FCOM_ERR_UNSUPP;
		}
		*p_val = v;
	return 0;
}
