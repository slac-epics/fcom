/* Test program for xdr decoder/encoder */

/* Read a ASCII file (stdin) defining a sequence of
 * blobs and convert into an XDR stream.
 */

#define MAIN_NAME prototst
#include "mainwrap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>

#include <ctype.h>
#include <inttypes.h>

#define __INSIDE_FCOM__
#include <fcom_api.h>
#include <fcomP.h>

#include <fcom_proto.h>

#include <xdr_dec.h>

#include <rpc/xdr.h>

static int
fldcmp(const char *nm, uint32_t a, uint32_t b)
{
	if ( a != b ) {
		fprintf(stderr,"blobcmp: %s mismatch 0x%"PRIx32" != 0x%"PRIx32"\n", nm, a, b);
		return -1;
	}
	return 0;
}

#define TSTFLD(p1,p2,fld) \
	if ( (err = fldcmp(#fld, (p1)->fc_##fld, (p2)->fcx_##fld)) ) \
		return err;

int
blobcmp(FcomBlob *pb, FcomBlob_XDR_ *pbx)
{
FcomBlob        *pbv1;
FcomBlobV1_XDR_ *pbxv1;
int             err;
void            *pd, *pdx;
int             sz;

	if (    ! FCOM_PROTO_MATCH( pb->fc_vers , FCOM_PROTO_VERSION_1x )
	     || ! FCOM_PROTO_MATCH( pbx->fcx_vers, FCOM_PROTO_VERSION_1x ) ) {
		return FCOM_ERR_BAD_VERSION;
	}

	pbv1  = pb;
	pbxv1 = &pbx->FcomBlob_XDR__u._v1;

	TSTFLD(pbv1, pbxv1, idnt);
	TSTFLD(pbv1, pbxv1, res3);
	TSTFLD(pbv1, pbxv1, tsHi);
	TSTFLD(pbv1, pbxv1, tsLo);
	TSTFLD(pbv1, pbxv1, stat);
	TSTFLD(pbv1, pbxv1, type);
	TSTFLD(pbv1, pbxv1, nelm);

	switch ( pbv1->fc_type ) {
		case FCOM_EL_FLOAT:
			sz = sizeof(float);    pd = pbv1->fc_flt; pdx = pbxv1->fcx_flt;
		break;

		case FCOM_EL_DOUBLE:
			sz = sizeof(double);   pd = pbv1->fc_dbl; pdx = pbxv1->fcx_dbl;
		break;

		case FCOM_EL_UINT32:
			sz = sizeof(uint32_t); pd = pbv1->fc_u32; pdx = pbxv1->fcx_u32;
		break;

		case FCOM_EL_INT32:
			sz = sizeof(int32_t);  pd = pbv1->fc_i32; pdx = pbxv1->fcx_i32;
		break;

		case FCOM_EL_INT8:
			sz = sizeof(int8_t);   pd = pbv1->fc_i08; pdx = pbxv1->fcx_i08;
		break;


		default:
			fprintf(stderr,"blobcmp: bad element type %i\n" ,pbv1->fc_type);
			return -1;
	}

	if ( memcmp(pd, pdx, sz*pbv1->fc_nelm) ) {
		fprintf(stderr,"blobcmp: payload data mismatch\n");
		return -1;
	}

	return 0;
}

#define XMEMSZ 10000
#define BLOBSZ 2000
#define NBLOBS 100

static __inline__ uint32_t SWAP32(uint32_t a)
{
union {
	uint32_t l;
	uint8_t  c[4];
} le = {l:1};

	if ( le.c[0] ) {
		a = ((a & 0x0000ffff) << 16) | ((a & 0xffff0000) >> 16);
		a = ((a & 0x00ff00ff) <<  8) | ((a & 0xff00ff00) >>  8);
	}
	return a;
}

int main(int argc, char **argv)
{
FcomBlobRef   pb[NBLOBS] = {0};
uint32_t      *xmem = malloc(XMEMSZ); 
uint32_t      *ptr;
uint32_t      gid,nblb;
XDR           xdr;
FcomMsg_XDR_  mx;
int           i,err,rval=1,nblobs;
FILE          *infile, *outfile;
char          *ifn = 0, *ofn = 0;
GETOPTSTAT_DECL;

	infile  = stdin;
	outfile = stdout;

	memset(&mx,0,sizeof(mx));
	xdrmem_create(&xdr, (char*)xmem, XMEMSZ, XDR_DECODE);

	while ( (i = getopt(argc, argv, "qf:o:")) > 0 ) {
		switch (i) {
			case 'q': outfile = 0;      break;
			case 'f': ifn     = optarg; break;
			case 'o': ofn     = optarg; break;

			default:
			break;
		}
	}

	if ( ifn ) {
		if ( ! (infile = fopen(ifn,"r")) ) {
			fprintf(stderr,"Opening %s for reading failed (%s)\n", ifn, strerror(errno));
			ofn = 0;
			ifn = 0;
			goto bail;
		}
	}

	if ( ofn ) {
		if ( ! (outfile = fopen(ofn,"w")) ) {
			fprintf(stderr,"Opening %s for writing failed (%s)\n", ifn, strerror(errno));

			ofn = 0;
			goto bail;
		}
	}

	for (i=0; i<NBLOBS; i++) {
    	pb[i] = malloc(BLOBSZ);
	}

	if ( (err = fcom_msg_init(xmem, XMEMSZ, FCOM_GID_ANY)) < 0 ) {
		fprintf(stderr,"fcom_msg_init error %s\n", fcomStrerror(err));
		goto bail;
	}

	nblobs = 0;

	while ( (err = fcom_get_blob_from_file(infile, pb[nblobs], BLOBSZ)) > 0 ) {

		if ( ( err = fcom_msg_append_blob(xmem, pb[nblobs]) ) < 0 ) {
			fprintf(stderr,"XDR encoder error %s\n", fcomStrerror(err));
			goto bail;
		}

		nblobs++;
	}

	if ( err < 0 ) {
		fprintf(stderr,"reading blobs from file error\n");
		goto bail;
	}

	fcom_msg_end(xmem, &gid, &nblb);

	if ( ! xdr_FcomMsg_XDR_(&xdr, &mx) ) {
		fprintf(stderr,"XDR reference decoder error\n");
		goto bail;
	} else {
		if ( nblobs != mx.FcomMsg_XDR__u.fcm_blobs.fcm_blobs_len ) {
			fprintf(stderr,"blob count mismatch\n");
			goto bail;
		}
		for ( i=0; i<nblobs; i++ ) {
			if ( blobcmp(pb[i], &mx.FcomMsg_XDR__u.fcm_blobs.fcm_blobs_val[i]) ) {
				fprintf(stderr,"BLOB #%i MISMATCH (encoding)\n",i);
				goto bail;
			}
		}
	}

	/* re-encode using reference encoder */
	xdr_destroy(&xdr);
	memset(xmem, 0, XMEMSZ);
	xdrmem_create(&xdr, (char*)xmem, XMEMSZ, XDR_ENCODE);

	if ( ! xdr_FcomMsg_XDR_(&xdr, &mx) ) {
		fprintf(stderr,"XDR reference encoder error\n");
		goto bail;
	} else {
		if ( ! FCOM_PROTO_MATCH( FCOM_PROTO_VERSION_1x, SWAP32(xmem[0])) ) {
			fprintf(stderr, "message version mismatch\n");
			goto bail;
		}
		nblobs = SWAP32(xmem[1]);
		if ( nblobs != mx.FcomMsg_XDR__u.fcm_blobs.fcm_blobs_len ) {
			fprintf(stderr,"blob count mismatch\n");
			goto bail;
		}
		ptr = xmem + 2;
		for ( i=0; i<nblobs; i++ ) {
			memset(pb[i],0,BLOBSZ);
			if ( (err = fcom_xdr_dec_blob(pb[i], BLOBSZ, ptr)) < 0 ) {
				fprintf(stderr,"XDR decoder error %i\n", err);
			}
			if ( blobcmp(pb[i], &mx.FcomMsg_XDR__u.fcm_blobs.fcm_blobs_val[i]) ) {
				fprintf(stderr,"BLOB #%i MISMATCH (decoding)\n", i);
				goto bail;
			}
			if ( outfile )
				fcom_put_blob_to_file(outfile, pb[i]);
			ptr += err;
		}
		if ( outfile )
			fprintf(outfile,"EF 0\n");
	}


	rval = 0;

bail:
	xdr_destroy(&xdr);
	xdr_free((xdrproc_t)xdr_FcomMsg_XDR_, (char*)&mx);
	for ( i=0; i<NBLOBS; i++ )
		free(pb[i]);
	free(xmem);
	if ( ifn )
		fclose(infile);
	if ( ofn )
		fclose(outfile);
	return rval;
}
