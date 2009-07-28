/* $Id: xdr_dec.c,v 1.1.1.1 2009/07/28 17:57:07 strauman Exp $ */

/* FCOM XDR decoder
 *
 * Author: Till Straumann <strauman@slac.stanford.edu>, 2009.
 */

#include <stdint.h>
#include <string.h>
#define __INSIDE_FCOM__
#include <fcom_api.h>
#include <fcomP.h>

#include <xdr_dec.h>

#include <xdr_swpP.h>

int
fcom_xdr_peek_size_id(int *p_sz, FcomID *p_id, uint32_t *xdr)
{
uint32_t type;
uint32_t nelm;
uint32_t vers;
int      sz;

	/* Verify that we know how to deal with this protocol version */
	vers = SWAPU32(xdr[0]);

	if ( FCOM_PROTO_MATCH(vers, FCOM_PROTO_VERSION_1x) ) {

		/* Peek at ID, type and # of elements */
		*p_id = SWAPU32(xdr[1]);
		type  = SWAPU32(xdr[6]);
		nelm  = SWAPU32(xdr[7]);

		/* compute total size in bytes of C-representation
		 * and # of 32-bit words in the XDR stream.
		 */
		if ( (sz = FCOM_EL_SIZE(type)) < 0 )
			return FCOM_ERR_INVALID_TYPE;

		*p_sz = nelm * sz + sizeof(FcomBlobHdr);

		switch ( type ) {
			case FCOM_EL_DOUBLE:
				nelm *= 2;
			break;

			case FCOM_EL_INT8:
				nelm = (nelm + 3)/4;
			break;

			default:
			/* includes:
			case FCOM_EL_FLOAT:
			case FCOM_EL_UINT32:
			case FCOM_EL_INT32:
			*/
			break;
		}
		return nelm + 8;
	}
	return FCOM_ERR_BAD_VERSION;
}

/* Decode a blob from a XDR stream in memory */

int
fcom_xdr_dec_blob(FcomBlobRef pb, int avail, uint32_t *xdr)
{
register int sz    = 0;
#ifndef __BIG_ENDIAN__
register int i;
#endif
uint32_t     *xdro = xdr;

	if ( (avail -= sizeof(pb->fc_vers)) < 0 )
		return FCOM_ERR_NO_SPACE;

#ifdef __PPC__
	/* zero a cache-line to speed up things */
	if ( (((uintptr_t)pb) & 31) == 0 )
		__dcbz(pb);
#endif

	pb->fc_vers = SWAPU32( *xdr++ );

	switch ( FCOM_PROTO_MAJ_GET(pb->fc_vers) ) {
		default:
		break;

		case FCOM_PROTO_VERSION_1x:
		{
			FcomBlobRef pbv1 = pb;

			pbv1->fc_raw = (void*)FC_ALIGN(pbv1+1);
			avail -= (uintptr_t)pbv1->fc_raw - (uintptr_t)(pbv1+1);

			sz = sizeof(pbv1->hdr) - sizeof(pbv1->fc_vers);

			if ( (avail -= sz) < 0 )
				return FCOM_ERR_NO_SPACE;

			pbv1->fc_idnt = SWAPU32(*xdr++);
			pbv1->fc_res3 = SWAPU32(*xdr++);
			pbv1->fc_tsHi = SWAPU32(*xdr++);
			pbv1->fc_tsLo = SWAPU32(*xdr++);
			pbv1->fc_stat = SWAPU32(*xdr++);
			pbv1->fc_type = SWAPU32(*xdr++);
			pbv1->fc_nelm = SWAPU32(*xdr++);

			if ( ( sz = FCOM_EL_SIZE(pbv1->fc_type) ) < 0 )
				return FCOM_ERR_INVALID_TYPE;

			sz *= pbv1->fc_nelm;

			if ( (avail -= sz) < 0 )
				return FCOM_ERR_NO_SPACE;

#ifdef __BIG_ENDIAN__
			memcpy( pbv1+1, xdr, sz );
#else
			switch (pbv1->fc_type) {
				case FCOM_EL_UINT32:
				case FCOM_EL_INT32:
				case FCOM_EL_FLOAT:
					for ( i=0; i<pbv1->fc_nelm; i++ ) {
						pbv1->fc_u32[i] = SWAPU32(xdr[i]);
					}
				break;

				case FCOM_EL_INT8:
					memcpy( pbv1+1, xdr, sz );
				break;

				case FCOM_EL_DOUBLE:
					for ( i=0; i<pbv1->fc_nelm*2; i+=2 ) {
						union {
							double   d;
							uint32_t l[2];
						} d_u;
						d_u.l[0] =  SWAPU32(xdr[i+1]);
						d_u.l[1] =  SWAPU32(xdr[i  ]);
						pbv1->fc_dbl[i/2] = d_u.d;
					}
				break;
			}
#endif
			xdr += (sz+sizeof(*xdr)-1)/sizeof(*xdr);
		}
		return xdr - xdro;
	}
	return FCOM_ERR_BAD_VERSION;
}

/* Decode a group (aka 'message') header from a XDR stream in memory */
int
fcom_xdr_dec_msghdr(uint32_t *xdrmem, int *p_nblobs)
{
	/* First 32-bit word is version number; for version 1.x
	 * the second 32-bit word is the blob count.
	 */
	if ( FCOM_PROTO_MATCH( SWAPU32(xdrmem[0]), FCOM_PROTO_VERSION_1x ) ) {
		*p_nblobs = SWAPU32(xdrmem[1]);
		/* Return the number of 32-bit words read */
		return 2;
	}
	return FCOM_ERR_BAD_VERSION;
}
