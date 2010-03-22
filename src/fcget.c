/*$Id: fcget.c,v 1.2 2010/03/19 23:45:44 strauman Exp $*/

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
	fprintf(stderr,"Usage: %s [-ahv] [-t <timeout_ms>] [-p <fcom_mc_prefix>] [-i <fcom_mc_IF>] blob_id\n", nm);
	fprintf(stderr,"  Options:\n");
	fprintf(stderr,"       -h print this message\n");
	fprintf(stderr,"       -a enforce asynchronous 'get'\n");
	fprintf(stderr,"       -t <timeout_ms> to wait for new data (or delay\n");
	fprintf(stderr,"          after subscription until attempting async get).\n");
	fprintf(stderr,"          Defaults to 1000.\n");
	fprintf(stderr,"       -p <fcom_mc_prefix>. Multicast prefix for FCOM\n");
	fprintf(stderr,"       -i <fcom_mc_IF>. IF (dot-address) on which to listen for FCOM\n");
	fprintf(stderr,"       -v verbose mode.\n");
	fprintf(stderr,"  Environment:\n");
	fprintf(stderr,"       FCOM_MC_PREFIX defines multicast prefix (overridden by -p)\n");
	fprintf(stderr,"       FCOM_MC_IFADDR defines address of IF to be listened on\n");
}

int
main(int argc, char **argv)
{
int      ch;
int      rval    = 1;
unsigned tout_ms = 1000;
int      async   = 0;
uint64_t llid;
uint32_t idnt    = FCOM_ID_NONE;
int      st;
char     *prefix = 0;
char     *mcifad = 0;
struct timespec tout;
FcomBlobRef     blob;
int      level   = 0;
uint32_t ifaddr;


	while ( (ch = getopt(argc, argv, "vht:ap:")) >= 0 ) {
		switch (ch) {
			case 'h':
				rval = 0;
			default:
				usage(argv[0]);
				return rval;

			case 'a':
				async = 1;
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

	if ( optind >= argc || 1 != sscanf(argv[optind],"%"SCNi64,&llid) ) {
		fprintf(stderr,"Missing or non-numeric FCOM blob ID\n");
		usage(argv[0]);
		return 1;
	}
	if ( mcifad ) {
		if ( INADDR_NONE == (ifaddr = inet_addr(mcifad)) ) {
			fprintf(stderr,"Invalid IP address: %s\n", mcifad);
			return 1;
		}
		udpCommSetIfMcastInp(ifaddr);
	}

	fcom_silent_mode = 1;
	st = fcomInit( prefix, 10 );

	if ( st ) {
		fprintf(stderr,"Unable to initialize FCOM: %s\n", fcomStrerror(st));
		return 1;
	}

	idnt = (uint32_t)llid;

	st = fcomSubscribe( idnt, async ? FCOM_ASYNC_GET : FCOM_SYNC_GET );

	if ( FCOM_ERR_UNSUPP == st ) {
		fprintf(stderr,"Warning: synchronous get not supported; using asynchronous mode\n");
		async = 1;
		st    = fcomSubscribe( idnt, FCOM_ASYNC_GET );
	}

	if ( st ) {
		fprintf(stderr,"FCOM subscription failed: %s\n", fcomStrerror(st));
		return 1;
	}

	if ( async ) {
		tout.tv_sec  = tout_ms/1000;
		tout.tv_nsec = (tout_ms - tout.tv_sec*1000)*1000000;
		if ( nanosleep( &tout, 0 ) ) {
			fprintf(stderr,"Sleep aborted\n");
			goto bail;
		}
	} else {
		st = fcomGetBlob( idnt, &blob, tout_ms );
		if ( st ) {
			fprintf(stderr,"fcomGetBlob(SYNCH) failed: %s\n", fcomStrerror(st));
			goto bail;
		}
		fcomReleaseBlob(&blob);
	}

	st = fcomDumpIDStats( idnt, level, stdout );
	if ( st < 0 ) {
		fprintf(stderr,"fcomDumpIDStats failed: %s\n", fcomStrerror(st));
		goto bail;
	}

	rval = 0;

bail:
	if ( FCOM_ID_NONE != idnt )
		fcomUnsubscribe( idnt );
	return rval;
}
