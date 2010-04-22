/*$Id: fcget.c,v 1.4 2010/04/22 02:16:31 strauman Exp $*/

/* Get FCOM Blob and dump to stdout */

#include <fcom_api.h>
#include <udpComm.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>

#include <fcomP.h> /* for fcom_silent_mode which is not really public */


static void
usage(char *nm)
{
	fprintf(stderr,"Usage: %s [-ahvs] [-t <timeout_ms>] [-p <fcom_mc_prefix>] [-i <fcom_mc_IF>] [-b bufs] blob_id {blob_id}\n", nm);
	fprintf(stderr,"  Options:\n");
	fprintf(stderr,"       -h print this message\n");
	fprintf(stderr,"       -a enforce asynchronous 'get'\n");
	fprintf(stderr,"       -b configure number of buffers (if you use many IDs); default=10\n");
	fprintf(stderr,"       -t <timeout_ms> to wait for new data (or delay\n");
	fprintf(stderr,"          after subscription until attempting async get).\n");
	fprintf(stderr,"          Defaults to 1000.\n");
	fprintf(stderr,"       -p <fcom_mc_prefix>. Multicast prefix for FCOM\n");
	fprintf(stderr,"       -i <fcom_mc_IF>. IF (dot-address) on which to listen for FCOM\n");
	fprintf(stderr,"       -v verbose mode.\n");
	fprintf(stderr,"       -s dump statistics before terminating.\n");
	fprintf(stderr,"  Environment:\n");
	fprintf(stderr,"       FCOM_MC_PREFIX defines multicast prefix (overridden by -p)\n");
	fprintf(stderr,"       FCOM_MC_IFADDR defines address of IF to be listened on\n");
}

int
main(int argc, char **argv)
{
int             ch;
int             rval    = 1;
unsigned        tout_ms = 1000;
int             async   = 0;
uint64_t        llid;
int             st;
char            *prefix = 0;
char            *mcifad = 0;
struct timespec tout;
FcomBlobRef     blob;
int             level   = 0;
uint32_t        ifaddr;
FcomBlobSetRef  set     = 0;
FcomID          *idnts  = 0;
int             nids    = 0;
int             nsubs   = 0;
int             stats   = 0;
int             bufs    = 10;
int             i;

	while ( (ch = getopt(argc, argv, "ab:hp:st:v")) >= 0 ) {
		switch (ch) {
			case 'h':
				rval = 0;
			default:
				usage(argv[0]);
				return rval;

			case 'a':
				async = 1;
			break;

			case 'b':
				if ( 1 != sscanf(optarg, "%i", &bufs) || bufs < 1) {
					fprintf(stderr,"Invalid arg to -b: must be positive # of buffers\n");
					usage(argv[0]);
					return 1;
				}
			break;

			case 's':
				stats = 1;
			break;

			case 't':
				if ( 1 != sscanf(optarg,"%u",&tout_ms) ) {
					fprintf(stderr,"Invalid 'timout_ms' argument\n");
					usage(argv[0]);
					return 1;
				}
			break;

			case 'p':
				prefix = optarg;
			break;

			case 'i':
				mcifad = optarg;
			break;

			case 'v':
				level = 1;
			break;
		}
	}

	if ( !prefix && !(prefix = getenv("FCOM_MC_PREFIX")) ) {
		fprintf(stderr,"Missing FCOM multicast prefix. Use '-p' option of define FCOM_MC_PREFIX env-var\n");
		return 1;
	}

	if ( !mcifad ) {
		mcifad = getenv("FCOM_MC_IFADDR");
		/* Don't complain if NULL - OK if routing tables are used
		 * or only a single NIC present.
		 */
	}

	nids = argc - optind;

	if ( nids <= 0 ) {
		fprintf(stderr,"Missing or non-numeric FCOM blob ID\n");
		usage(argv[0]);
		return 1;
	}

	if ( ! (idnts = malloc( nids * sizeof(FcomID) )) ) {
		fprintf(stderr,"Not enough memory (for ID array)\n");
		return 1;
	}

	for ( i=0; i<nids; i++ ) {
 		if (  1 != sscanf(argv[optind+i],"%"SCNi64,&llid) ) {
			fprintf(stderr,"Non-numeric FCOM blob ID (#%i)\n", i+1);
			usage(argv[0]);
			goto bail;
		}
		idnts[i] = (FcomID)llid;
	}

	if ( mcifad ) {
		if ( INADDR_NONE == (ifaddr = inet_addr(mcifad)) ) {
			fprintf(stderr,"Invalid IP address: %s\n", mcifad);
			goto bail;
		}
		udpCommSetIfMcastInp(ifaddr);
	}

	fcom_silent_mode = 1;
	st = fcomInit( prefix, bufs );

	if ( st ) {
		fprintf(stderr,"Unable to initialize FCOM: %s\n", fcomStrerror(st));
		goto bail;
	}

	if ( nids > 1 )
		async = 1;

	for ( i=0; i<nids; i++ ) {
		st = fcomSubscribe( idnts[i], async ? FCOM_ASYNC_GET : FCOM_SYNC_GET );

		if ( FCOM_ERR_UNSUPP == st ) {
			fprintf(stderr,"Warning: synchronous get not supported; using asynchronous mode\n");
			async = 1;
			st    = fcomSubscribe( idnts[i], FCOM_ASYNC_GET );
		}

		if ( st ) {
			fprintf(stderr,"FCOM subscription failed: %s\n", fcomStrerror(st));
			goto bail;
		}
		nsubs++;
	}

	if ( nids > 1 ) {
		/* build a set */
		st = fcomAllocBlobSet( idnts, nids, &set );
		if ( st ) {
			fprintf(stderr,"fcomAllocBlobSet failed: %s\n", fcomStrerror(st));
			set = 0;
			goto bail;
		}

		st = fcomGetBlobSet( set, 0, (1<<nids) - 1, FCOM_SET_WAIT_ALL, tout_ms );
		if ( st ) {
			fprintf(stderr,"fcomGetBlobSet failed: %s\n", fcomStrerror(st));
			if ( FCOM_ERR_TIMEDOUT != st )
				goto bail;
		}

		for ( i=0; i<nids; i++ ) {
			fcomDumpBlob( set->memb[i].blob, level, stdout );
		}
	} else {
		if ( async ) {
			tout.tv_sec  = tout_ms/1000;
			tout.tv_nsec = (tout_ms - tout.tv_sec*1000)*1000000;
			if ( nanosleep( &tout, 0 ) ) {
				fprintf(stderr,"Sleep aborted\n");
				goto bail;
			}
		}

		st = fcomGetBlob( idnts[0], &blob, async ? 0 : tout_ms );
		if ( st ) {
			fprintf(stderr,"fcomGetBlob(%sSYNCH) failed: %s\n", async ? "A":"", fcomStrerror(st));
			goto bail;
		}
		st = fcomDumpBlob( blob, level, stdout );
		fcomReleaseBlob(&blob);
		if ( st < 0 ) {
			fprintf(stderr,"fcomDumpBlob failed: %s\n", fcomStrerror(st));
			goto bail;
		}
	}

	rval = 0;

bail:

	if ( stats )
		fcomDumpStats(stdout);

	if ( set ) {
		st = fcomFreeBlobSet( set );
		if ( st )
			fprintf(stderr,"fcomFreeBlobSet failed: %s\n", fcomStrerror(st));
	}

	for ( i=0; i<nsubs; i++ ) {
		st = fcomUnsubscribe( idnts[i] );
		if ( st )
			fprintf(stderr,"fcomUnsubscribe("FCOM_ID_FMT") failed: %s\n", idnts[i], fcomStrerror(st));
	}

	free( idnts );

	return rval;
}
