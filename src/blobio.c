/* $Id: blobio.c,v 1.3 2010/04/22 01:56:07 strauman Exp $ */

/* Read/write a ASCII file (stdin) defining a sequence of
 * blobs and convert to/from C-representation.
 *
 * FILE FORMAT:
 *
 * file:   { record } EF 0
 *
 * record:
 *   ve <version>
 *   id <fcom ID>
 *   th <tstmpHi>
 *   tl <tstmpLo>
 *   st <status >
 *   ty <type   > <count>
 *      0: none, 1: float, 2: ddouble, 3: uint32, 4: int32, 5: int8
 *   <element>
 *   <element>
 *   ...
 *   ER 0
 *
 * Author: Till Straumann <strauman@slac.stanford.edu>, 2009.
 *
 * This code is intended FOR TESTING PURPOSES.
 *
 * The ASCII file format is described in <fcomP.h>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>
#include <inttypes.h>

#define __INSIDE_FCOM__
#include <fcom_api.h>
#include <fcomP.h>

/* slurp one char from stdio stream 'f' */
#define GCHR(f) if ( (ch = fgetc(f)) < 0 ) return -1;

/* Scan a uint32_t number with '%i' format.
 * We like %i because it allows the user to
 * specify the base (by means of '0x' or '0'
 * prefix) but %i is always a *signed* number,
 * i.e., we cannot use large unsigned numbers
 * in hex notation (w/o '-' sign) because these
 * are capped to 0x7fffffff.
 * This routine works around this limitation
 * by scanning into a (signed) 64-bit variable
 * and truncating to 32-bits.
 * 
 * RETURNS: 1 on success, 0 if no number could be
 *          scanned and <0 on error (such as closed
 *          file descriptor).
 */
static int
convl(FILE *f, uint32_t *pv)
{
int     rval;
int64_t val;
/* must scan 64-bit int in order to cover full range
 * of uint32_t -- and there is no 'unsigned' base-0
 * conversion :-(
 */
	rval = fscanf(f,"%"SCNi64,&val);
	if ( rval < 0 )
		perror("fscanf");
	*pv = (uint32_t)(val & 0xffffffff);
	return rval;
}

/* Scan a 'key' 'uint32_t-value' pair storing 'key'
 * (a string) into 'fld' and the value into *pv.
 * Only the first two characters are stored into
 * '*key' - any more alpha-numeric chars are skipped.
 * Hence, 
 *   ve 0x0000fc11
 * and
 *   version 0x0000fc11
 * will both yield 'fld -> "ve"', *pv -> 0xfc11.
 *
 * RETURNS: 1 on success.
 */
static int 
scanl(FILE *f, char *fld, uint32_t *pv)
{
int     ch;

	GCHR(f);
	if ( !isalnum(ch) ) {
		fprintf(stderr,"Not an alpha-numerical character: '%c'\n", ch);
		return -1;
	}
	*fld++ = ch;
	GCHR(f);
	if ( isalnum(ch) ) {
		*fld++ = ch;
		do {
			GCHR(f);
		} while ( !isspace(ch) );
	}
	*fld = 0;
	return convl(f, pv);
}

/* Eat chars until '\n' is read and skipped;
 * the next character read from 'f' is the first
 * one on a new line
 */
static int skiptoeol(FILE *f)
{
int ch;
		GCHR(f);
		/* skip to EOL */
		while ( '\n' != ch ) {
			GCHR(f);
		}
		return 0;
}

/* Return size needed to store a FCOM element of type pb->fc_type
 * or a negative error code.
 */
static int
tsz(FcomBlobRef pb)
{
	switch ( pb->fc_type ) {
		default:
			fprintf(stderr,"Bad element type %"PRIu32"\n", (uint32_t)pb->fc_type);
		break;

		case FCOM_EL_FLOAT:  return sizeof(float);
		case FCOM_EL_DOUBLE: return sizeof(double);
		case FCOM_EL_UINT32: return sizeof(uint32_t);
		case FCOM_EL_INT32:  return sizeof(int32_t);
		case FCOM_EL_INT8:   return sizeof(int8_t);
	}
	return -1;
}

/* Scan a blob in ASCII representation from a file 'f' and store
 * into *pb. Pass the available amount of memory for storage
 * in 'avail'.
 * 
 * RETURNS: - number of bytes stored into *pb         OR
 *          - zero if 'EF 0' (eof) marker was reached OR
 *          - value < 0 on error.
 */

int
fcom_get_blob_from_file(FILE *f, FcomBlobRef pb, int avail)
{
uint32_t u;
int      k,sz=0,err;
char     key[10];
int      rval;

	if ( (avail-=sizeof(*pb)) < 0 ) {
		fprintf(stderr,"No memory\n");
		return -1;
	}

	/* First zero out */
	memset(pb, 0, sizeof(*pb));
	while ( (rval = scanl(f, key, &u)) > 0 ) {
		if        ( ! strcmp(key,"ve") ) {
			pb->fc_vers = u;
		} else if ( ! strcmp(key,"id") ) {
			pb->fc_idnt = u;
		} else if ( ! strcmp(key,"th") ) {
			pb->fc_tsHi = u;
		} else if ( ! strcmp(key,"tl") ) {
			pb->fc_tsLo = u;
		} else if ( ! strcmp(key,"st") ) {
			pb->fc_stat = u;
		} else if ( ! strcmp(key,"ty") ) {
			switch ( u ) {
				case FCOM_EL_TYPE(FCOM_EL_FLOAT):  u = FCOM_EL_FLOAT;  break;
				case FCOM_EL_TYPE(FCOM_EL_DOUBLE): u = FCOM_EL_DOUBLE; break;
				case FCOM_EL_TYPE(FCOM_EL_UINT32): u = FCOM_EL_UINT32; break;
				case FCOM_EL_TYPE(FCOM_EL_INT32):  u = FCOM_EL_INT32;  break;
				case FCOM_EL_TYPE(FCOM_EL_INT8):   u = FCOM_EL_INT8;   break;
				default:
					fprintf(stderr,"Bad element type %"PRIu32"\n", u);
					return -1;
			}
			pb->fc_type = u;
			if ( 1 != convl(f,&u) ) {
				fprintf(stderr,"Bad element count\n");
				return -1;
			}
			pb->fc_nelm = u;

			if ( (k = tsz(pb)) < 0 ) 
				return -1;

			sz = k*pb->fc_nelm;

			if ( (avail -= sz) < 0 ) {
				fprintf(stderr,"No memory\n");
				return -1;	
			}

			pb->fc_raw = (void*)FC_ALIGN((pb+1));

			for ( k=0; k<pb->fc_nelm; k++ ) {
				switch ( pb->fc_type ) {
					default:
						/* should never get here -- but silences compiler warning */
						err = 0;
					break;

					case FCOM_EL_FLOAT:
						err = fscanf(f,"%f",&pb->fc_flt[k]);
					break;
					case FCOM_EL_DOUBLE:
						err = fscanf(f,"%lf",&pb->fc_dbl[k]);
					break;
					case FCOM_EL_UINT32:
						err = convl(f,&pb->fc_u32[k]);
					break;
					case FCOM_EL_INT32:
						err = convl(f,(uint32_t*)&pb->fc_i32[k]);
					break;
					case FCOM_EL_INT8:
						err = fscanf(f,"%"SCNi8, &pb->fc_i08[k]);
					break;
				}
				if ( 1 != err ) {
					fprintf(stderr,"Read bad element #%i (type %"PRIu32")\n", k, (uint32_t)FCOM_EL_TYPE(pb->fc_type));
				}
			}
		} else if ( ! strcmp(key,"ER") ) {
			skiptoeol(f);
			return sizeof(*pb)+sz;
		} else if ( ! strcmp(key,"EF") ) {
			skiptoeol(f);
			return 0;
		} else {
			fprintf(stderr,"Bad key %s\n",key);
			return -1;
		}
		skiptoeol(f);
	}
	return rval;
}

/* Write a blob in ASCII representation to a file 'f'.
 * 
 * RETURNS: zero on success, nonzero on error.
 * 
 * NOTE:    terminating 'EF 0' (eof) marker must be
 *          appended by caller.
 */
int
fcom_put_blob_to_file(FILE *f, FcomBlobRef pb)
{
int     k;

	fprintf(f,"ve 0x%08"PRIx32"\n",(uint32_t)pb->fc_vers);
	fprintf(f,"id 0x%08"PRIx32"\n",pb->fc_idnt);
	fprintf(f,"th 0x%08"PRIx32"\n",pb->fc_tsHi);
	fprintf(f,"tl 0x%08"PRIx32"\n",pb->fc_tsLo);
	fprintf(f,"st 0x%08"PRIx32"\n",pb->fc_stat);
	fprintf(f,"ty 0x%08"PRIx32" %3"PRIu32"\n", (uint32_t)FCOM_EL_TYPE(pb->fc_type), (uint32_t)pb->fc_nelm);
	for ( k=0; k<pb->fc_nelm; k++ ) {
		switch (pb->fc_type) {
			default:
				fprintf(stderr,"Blob has bad type %"PRIu32"\n", (uint32_t)pb->fc_type);
			return -1;
			case FCOM_EL_FLOAT:  fprintf(f, "   %15.10g\n", pb->fc_flt[k]); break;
			case FCOM_EL_DOUBLE: fprintf(f, "   %15.10g\n", pb->fc_dbl[k]); break;
			case FCOM_EL_UINT32: fprintf(f, "   0x%08"PRIx32"\n", pb->fc_u32[k]); break;
			case FCOM_EL_INT32:  fprintf(f, "   0x%08"PRIx32"\n", pb->fc_i32[k]); break;
			case FCOM_EL_INT8:   fprintf(f, "   0x%02"PRIx8"\n", (uint8_t)pb->fc_i08[k]); break;
		}
	}

	fprintf(f,"ER 0\n");

	return 0;
}
