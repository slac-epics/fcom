/* $Id$ */

/* FCOM XDR encoder
 *
 * Author: Till Straumann <strauman@slac.stanford.edu>, 2009.
 */

#include <stdint.h>
#include <string.h>
#define __INSIDE_FCOM__
#include <fcom_api.h>

#include <xdr_dec.h>

#include <xdr_swpP.h>

int
fcom_xdr_enc_blob(uint32_t *xdr, FcomBlobRef pb, int avail, uint32_t *p_gid)
{
register int sz = 0, i;
uint32_t     *xdro = xdr;

	if ( (avail -= sizeof(pb->fc_vers)) < 0 )
		return FCOM_ERR_NO_SPACE;

#ifdef __PPC__
	/* zero a cache-line to speed up things */
	if ( (((uintptr_t)xdr) & 31) == 0 )
		__dcbz(pb);
#endif

	*xdr++ = SWAPU32(pb->fc_vers);

	switch ( FCOM_PROTO_MAJ_GET(pb->fc_vers) ) {
		default:
		break;

		case FCOM_PROTO_VERSION_1x:
		{
			FcomBlobV1Ref pbv1 = & pb->fcb_v1;

			/* make sure version encoded in ID matches fc_vers */
			if ( FCOM_PROTO_MAJ_1 != FCOM_GET_MAJ(pbv1->fc_idnt) )
				return FCOM_ERR_BAD_VERSION;

			sz = sizeof(*pbv1) - sizeof(pbv1->fc_vers);

			if ( (avail -= sz) < 0 )
				return FCOM_ERR_NO_SPACE;

			if ( ! FCOM_ID_VALID(pbv1->fc_idnt) )
				return FCOM_ERR_INVALID_ID;

			*p_gid = FCOM_GET_GID(pbv1->fc_idnt);

			*xdr++ = SWAPU32(pbv1->fc_idnt);
			*xdr++ = SWAPU32(pbv1->fc_res3);
			*xdr++ = SWAPU32(pbv1->fc_tsHi);
			*xdr++ = SWAPU32(pbv1->fc_tsLo);
			*xdr++ = SWAPU32(pbv1->fc_stat);
			*xdr++ = SWAPU32(pbv1->fc_type);
			*xdr++ = SWAPU32(pbv1->fc_nelm);

			if ( ( sz = FCOM_EL_SIZE(pbv1->fc_type) ) < 0 )
				return FCOM_ERR_INVALID_TYPE;

			sz *= pbv1->fc_nelm;

			if ( (avail -= sz) < 0 )
				return FCOM_ERR_NO_SPACE;

#ifndef __BIG_ENDIAN__
			switch (pbv1->fc_type) {
				case FCOM_EL_UINT32:
				case FCOM_EL_INT32:
				case FCOM_EL_FLOAT:
				{
				uint32_t *p_u32 = pbv1->fc_u32;

					for ( i=0; i<pbv1->fc_nelm; i++ ) {
						xdr[i] = SWAPU32(p_u32[i]);
					}
					xdr += i;
				}
				break;

				case FCOM_EL_INT8:
#endif
					/* On BE architectures this is executed for all types */
				{
				int8_t *p_i08 = pbv1->fc_i08;

					memcpy( xdr, p_i08, sz );
					xdr += sz/sizeof(*xdr);
					/* pad with zeroes */
					if ( (i = sz - (sz/sizeof(*xdr))*sizeof(*xdr)) > 0 ) {
						memset( (void*)xdr + i, 0, sizeof(*xdr) - i );
						xdr++;
					}
					sz += i;
				}
#ifndef __BIG_ENDIAN__
				break;

				case FCOM_EL_DOUBLE:
				{
				double *p_dbl = pbv1->fc_dbl;
					for ( i=0; i<pbv1->fc_nelm*2; i+=2 ) {
						union {
							double   d;
							uint32_t l[2];
						} d_u;
						d_u.d = p_dbl[i/2];
						xdr[i+1] = SWAPU32(d_u.l[0]);
						xdr[i  ] = SWAPU32(d_u.l[1]);
					}
					xdr += i;
				}
				break;
			}
#endif
		}
		return xdr - xdro;
	}
	return FCOM_ERR_BAD_VERSION;
}

/* Initialize 'message' / 'FcomGroup'.
 * We maintain internal state information in the
 * first two 32-bit words which eventually hold
 * the XDR encoded 'message' protocol version 
 * and the total number of blobs the 'message'
 * holds, respectively.
 *
 * We store the following parameters (as 4 16-bit
 * quantities):
 *
 *  - total size of '*xdrmem' in 32-bit words.
 *  - GID of the 'message'.
 *  - 'write-index'; index into xdrmem where we
 *    write the next encoded XDR 32-bit word.
 *  - number of blobs encoded.
 *
 * To avoid c99 aliasing issues we explicitly encode
 * the 16-bit quantities into 32-bit words.
 */
#define MSG_GET_GID(x) ((x)[1]>>16)
#define MSG_GET_IDX(x) ((x)[1] & 0xffff)
#define MSG_GET_SIZ(x) ((x)[0]>>16)
#define MSG_GET_NBL(x) ((x)[0] & 0xffff)

#define MSG_SET(x,size,gid,n_blobs,idx)       \
	do { (x)[0] = ((size) << 16) | (n_blobs); \
         (x)[1] = ((gid ) << 16) | (idx);     \
    } while (0)

#define MSG_SET_GIDIDX(x, gid, idx)           \
	do { (x)[1] = ((gid ) << 16) | (idx);     \
	} while (0)

#define MSG_GET_GIDIDX(x) ((x)[1])

#define MSG_INC_NBL(x)                        \
	do { (x)[0]++; } while (0)

int
fcom_msg_init(uint32_t *xdrmem, uint16_t size, uint32_t gid)
{
uint16_t idx  = 2, n_blobs = 0;

	if ( ! FCOM_GID_VALID(gid) && FCOM_GID_ANY != gid )
		return FCOM_ERR_INVALID_ID;

	if ( size < 2*sizeof(*xdrmem) ) {
		return FCOM_ERR_NO_SPACE;
	}

	/* convert size into index */
	size     /= sizeof(*xdrmem);
	/* store state-info into xdrmem */
	MSG_SET(xdrmem, size, gid, n_blobs, idx);

	/* 2 words 'written' so far -- actually, we just use
	 * those words for our internal business but eventually
	 * store the message version and blob-count there
	 * (fcom_msg_end()).
     */
	return 2;
}

int
fcom_msg_append_blob(uint32_t *xdrmem, FcomBlobRef pb)
{
int rval;
uint16_t idx, sz;
uint32_t gid, ogid;

	/* retrieve write index and total xdrmem size from xdrmem */
	idx  = MSG_GET_IDX(xdrmem);
    sz   = MSG_GET_SIZ(xdrmem);

	/* encode blob at xdrmem+idx, remaining size is (sz-idx)*4 */
	rval = fcom_xdr_enc_blob(xdrmem + idx, pb, (sz-idx) * sizeof(*xdrmem), &gid);

	/* retrieve GID from xdrmem */
	ogid = MSG_GET_GID(xdrmem);

	if ( FCOM_GID_ANY == gid ) {
		gid = ogid;
	} else {
		/* gid doesn't match existing ogid or is invalid */
		if ( (FCOM_GID_ANY != ogid && ogid != gid) || ! FCOM_GID_VALID(gid) )
			return FCOM_ERR_INVALID_ID;
	}

	if ( rval > 0 ) {
		/* advance write-index */
		idx += rval;
		/* store updated state info in xdrmem's first two 32-bit words */
		MSG_SET_GIDIDX(xdrmem, gid, idx);
		/* advance blob count */
		MSG_INC_NBL(xdrmem);
	}
	return rval;
}

/* Wrap up a message */
uint32_t
fcom_msg_end(uint32_t *xdrmem, uint32_t *p_gid, uint32_t *p_nblobs)
{
         /* retrieve blob count, gid and write index */
uint32_t nblb = MSG_GET_NBL(xdrmem);
uint32_t rval = MSG_GET_IDX(xdrmem);
uint32_t gid  = MSG_GET_GID(xdrmem);


	/* store protocol version and blob count in XDR stream head */
	xdrmem[0] = SWAPU32(FCOM_PROTO_VERSION_11);
	xdrmem[1] = SWAPU32(nblb);
	/* xdrmem now holds a properly encoded 'message' (FcomGroup) */

	/* return GID and write index (== total amount of data written)
	 * this info helps the caller to send the right amount of data
	 * to the correct multicast address.
	 * For informative purposes also return # of blobs contained in
	 * this message.
     */
	*p_gid    = gid;
	*p_nblobs = nblb;
	return rval;
}

/* Compact encoder for a message holding only a single blob */
int
fcom_msg_one_blob(uint32_t *xdrmem, uint16_t sz, FcomBlobRef pb, uint32_t *p_gid)
{
int s = sz - 2*sizeof(*xdrmem);
int rval;

	if ( s < 0 )
		return FCOM_ERR_NO_SPACE;

	/* store protocol version and blob count (1) in message header */
	xdrmem[0] = SWAPU32(FCOM_PROTO_VERSION_11);
	xdrmem[1] = SWAPU32(1);

	/* encode blob */
	rval = fcom_xdr_enc_blob(xdrmem + 2, pb, s, p_gid);

	if ( rval > 0 )
		rval += 2;

	/* return total amount of xdrmem written (# of 32-bit words) */
	return rval;
}
