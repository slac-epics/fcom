/* $Id: fcometst.c,v 1.2 2010/01/13 20:45:51 strauman Exp $ */

/* Echo a simple FCOM blob (for measuring protocol-overhead in a round-trip
 * situation)
 */

#define MAIN_NAME fcometst
#include "mainwrap.h"

#include <inttypes.h>
#include <getopt.h>
#include <unistd.h>

#include <fcom_api.h>
#include <fcomP.h>

#include <sys/time.h>

/* Wait for a blob to arrive and then echo back.
 * Assume first 32-bit word in the received blob
 * is the destination ID.
 */

int
fcomEchoBlob(FcomID id, unsigned timeout_ms)
{
int         st;
FcomBlobRef p_b;
FcomBlob    b;

	st = fcomGetBlob( id, &p_b, timeout_ms );
	if ( st ) {
		return st;
	}

	b      = *p_b;

	b.fc_idnt = p_b->fc_u32[0];

	fcomPutBlob( &b );

	fcomReleaseBlob( &p_b );

	return 0;
}

int
fcomPingBlob(FcomID dst1, FcomID dst2, int nelm)
{
FcomBlob       b;
FcomBlobRef    p_b;
uint32_t       data[nelm+1];
struct timeval t;
int            st;
uint32_t       usdel;

	gettimeofday( &t, 0 );

	b.fc_vers   = FCOM_PROTO_VERSION;
	b.fc_type   = FCOM_EL_UINT32;
	b.fc_nelm   = nelm + 1;
	b.fc_idnt   = dst1;
	b.fc_tsHi   = t.tv_sec;
	b.fc_tsLo   = t.tv_usec;
	b.fc_stat   = 0;
	b.fc_u32    = data;
	b.fc_u32[0] = dst2;

	st = fcomPutBlob( &b );
	if ( st ) {
		fprintf(stderr,"fcomPutBlob failed: %s\n", fcomStrerror(st));
		return st;
	}

	/* Note the race condition: if the peer replies before we get
	 * to block for reply then FCOM actually discards the reply!
	 *
	 * This can be worked around by scheduling the calling task
	 * at a higher priority than the FCOM receiver task...
	 */

	st = fcomGetBlob( dst2, &p_b, 1000);
	if ( st )
		return st;

	gettimeofday( &t, 0 );
	usdel = 0;
	if ( b.fc_tsLo > t.tv_usec ) {
		t.tv_sec--;	
		t.tv_usec += 1000000;
	}
	usdel  = t.tv_usec - b.fc_tsLo;
	usdel += 1000000 * (t.tv_sec - b.fc_tsHi);
	fcomReleaseBlob( &p_b );
	return usdel;
}

int fcom_etst_init()
{
	return fcomInit("239.255.0.0",100);
}


static void
usage(const char *nm)
{
	fprintf(stderr,"usage: %s [-h] [-t <timeout_ms>], [-l <loops>], [-x <return_ID>] [-n <nelms>] <dst_ID>\n", nm);
}

static int
getn(const char *nm, const char *buf, uint32_t *pi)
{
	if ( 1 != sscanf(buf,"%"SCNi32,pi) ) {
		usage(nm);
		return 1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
int      ch;
int      tx = 0;
uint32_t d1,d2,id = 0;
uint32_t to = 1000;
uint32_t l  = 10;
int      st;
uint32_t maxp = 0;
uint32_t nelm = 10;
int      rval = 1;
GETOPTSTAT_DECL;

	while ( (ch = getopt(argc, argv, "hx:t:l:n:")) > 0 ) {
		switch ( ch ) {
			default:
				usage(argv[0]);
				return 1;

			case 'h':
				usage(argv[0]);
				return 0;

			case 'x':
				tx = 1;
				if ( getn(argv[0], optarg, &d1) )
					return 1;
			break;

			case 'l':
				if ( getn(argv[0], optarg, &l) )
					return 1;
			break;

			case 'n':
				if ( getn(argv[0], optarg, &nelm) )
					return 1;
			break;


			case 't':
				if ( getn(argv[0],optarg, &to) )
					return 1;
			break;
		}
	}

	if ( optind >= argc ) {
		fprintf(stderr,"Missing argument\n");
		usage(argv[0]);
		return 1;
	}
	if ( getn(argv[0], argv[optind], &d2) )
		return 1;
	
	st = fcomInit("239.255.0.0", 100);

	if ( st ) {
		fprintf(stderr,"fcomInit() failed: %s\n", fcomStrerror(st));
		return 1;
	}

	id = tx ? d1 : d2;

	if ( (st = fcomSubscribe( id, FCOM_SYNC_GET ) ) ) {
		fprintf(stderr,"fcomSubscribe(0x%"PRIx32") failed: %s\n", id, fcomStrerror(st));

		id = 0;
		goto bail;
	}

	/* give the subscription some time... */
	sleep(2);

	if ( tx ) {
		while ( l-- > 0 ) {
			int32_t d;
			
			d = fcomPingBlob(d2, d1, nelm);
			if ( d < 0 ) {
				fprintf(stderr,"fcomPingBlob(0x%"PRIx32", 0x%"PRIx32") failed: %s\n", d2, d1, fcomStrerror(d));
			} else {
				if ( d > maxp )
					maxp = d;
			}
		}
		printf("Max round-trip time: %"PRIu32"us\n", maxp);
	} else {
		while ( l-- > 0 ) {
			if ( (st = fcomEchoBlob(d2, to)) ) {
				fprintf(stderr,"fcomEchoBlob(0x%"PRIx32") failed: %s\n", d2, fcomStrerror(st));
			}
		}
	}

	rval = 0;

bail:
	if ( id )
		fcomUnsubscribe( id );

	fcom_exit();

	return rval;
}
