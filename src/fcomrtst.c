/* $Id: fcomrtst.c,v 1.4 2010/03/18 18:21:47 strauman Exp $ */

/* Test program for FCOM receiver
 *
 * Author: Till Straumann <strauman@slac.stanford.edu>, 2009.
 *
 * Reads ASCII file from stdin (in fcom_get_blob_from_file() format,
 * see fcomP.h) and subscribes all IDs found.
 * Then (depending on implemented features) it either
 *  EITHER  repeatedly executes fcom_receiver() until the subscribed number
 *          of blobs has been processed.
 *  OR      sleeps for 'timeout' amount while data are received in the
 *          background.
 *  OR      performs a synchronous fcomGetBlob() on the LAST ID
 *
 * At this point, all data sent by a concurrently running 'fcomxtst'
 * should be processed and this program executes (asynchronous)
 * 'fcomGetBlob()' for all IDs and subsequently writes them to
 * an ASCII file.
 */
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/errno.h>
#include <time.h>

#include <fcom_api.h>
#include <fcomP.h>

#define BLOBSZ 1000

#define NUMID  100

int
main(int argc, char **argv)
{
int         st;
int         rval = 1;
FcomBlobRef pb = malloc(BLOBSZ);
int         err;
FILE        *infile  = stdin;
FILE        *outfile = stdout;
FcomBlobRef pb1;
FcomID      ids[NUMID];
int         nids = 0, nblobs = 0;
int         i;
unsigned    tout=10000;
#if !defined(USE_EPICS) && !defined(USE_PTHREADS)
int         got;
#else
struct timespec touts;
#endif
int         have_sync = FCOM_SYNC_GET;
char        *pref = "239.255.0.0:0";
FILE        *nif  = 0;
FILE        *nof  = 0;

	while ( (i=getopt(argc, argv, "at:p:i:o:")) > 0 ) {
		switch (i) {
			case 'a':
				/* disable synchronous gets */
				have_sync = FCOM_ASYNC_GET;
			break;

			case 'p':
				pref = optarg;
			break;

			case 'i':
				if ( !(nif = fopen(optarg,"r")) ) {
					fprintf(stderr,"Unable to open infile: %s\n", strerror(errno));
					goto bail;
				}
				infile = nif;
			break;
				
			case 'o':
				if ( !(nof = fopen(optarg,"w")) ) {
					fprintf(stderr,"Unable to open outfile: %s\n", strerror(errno));
					goto bail;
				}
				outfile = nof;
			break;
				
			case 't':
				if ( 1 != sscanf(optarg,"%u",&tout) ) {
					fprintf(stderr,"Number expected as '-t' argument\n");
					return 1;
				}
			break;

			case 'h':
			default:
				fprintf(stderr,"Usage: %s [-t <timeout>] [-p <fcom_mcprefix>] [-i <infile>] [-o <outfile>] [-a]\n", argv[0]);
			return 'h' == i ? 0 : 1;
		}
	}

	if ( (st = fcomInit(pref, 100)) ) {
		fprintf(stderr, "fcomInit() failed: %s\n", fcomStrerror(st));
		goto bail;
	}

	/* Check whether we have synchronous operations */
	if ( FCOM_SYNC_GET == have_sync ) {
		fprintf(stderr,"Checking for synchronous gets:");
		st = fcomSubscribe(FCOM_MAKE_ID(FCOM_GID_MIN, FCOM_SID_MIN), have_sync);
		if ( !st ) {
			fprintf(stderr,"OK\n");
			st = fcomUnsubscribe(FCOM_MAKE_ID(FCOM_GID_MIN, FCOM_SID_MIN));
		} else if ( FCOM_ERR_UNSUPP == st ) {
			fprintf(stderr,"NO -- not using\n");
			have_sync = FCOM_ASYNC_GET;
			st = 0;
		} 
		if ( st ) {
			fprintf(stderr,"ERROR: %s\n", fcomStrerror(st));
			goto bail;
		}
	}

    /* subscribe all IDs found in the input file */
	while ( (err = fcom_get_blob_from_file(infile, pb, BLOBSZ)) > 0 ) {
		if ( nids < NUMID ) {
			if ( (st = fcomSubscribe(ids[nids] = pb->fc_idnt, have_sync)) ) {
				fprintf(stderr,"fcomSubscribe() failed: %s\n", fcomStrerror(st));
			} else {
				nids++;
			}
		}
		nblobs++;
	}

#if 0
	/* Commented this test: there could be repeated definitions for as single ID
     * in the file...
     */
	if ( nblobs != nids )
		fprintf(stderr,"Warning: Not all blobs (# %u) subscribed (# %u)\n",nblobs, nids);
#endif

#if !defined(USE_EPICS) && !defined(USE_PTHREADS)
	for ( i=0; i<nids && (got = fcom_receive(tout)); i+=got)
		/* do nothing else */;
#else
	if ( !have_sync ) {
		touts.tv_sec  = tout/1000;
		touts.tv_nsec = (tout - touts.tv_sec *1000) * 1000000;
		nanosleep(&touts, 0);
	} else {
		/* block for *last* blob to arrive */
		if ( nids > 0 ) {
			fprintf(stderr,"Blocking for data...\n");
			st = fcomGetBlob(ids[nids-1], &pb1, tout);
			if ( st ) {
				fprintf(stderr,"ERROR: %s\n", fcomStrerror(st));
				goto bail;
			}
			fprintf(stderr,"...got it!\n");
			fcomReleaseBlob(&pb1);
		}
	}
#endif

	for ( i=0; i<nids; i++ ) {
		if ( (st=fcomGetBlob(ids[i], &pb1, 0)) ) {
			fprintf(stderr,"fcomGetBlob(0x%08"PRIx32") failed: %s\n", ids[i], fcomStrerror(st));
		} else {
			fcom_put_blob_to_file(outfile, pb1);
			fcomReleaseBlob(&pb1);
		}
	}

	if ( outfile )
		fprintf(outfile,"EF 0\n");

	fcom_recv_stats(stderr);

	for ( i=0; i<nids; i++) {
		if ( (st=fcomUnsubscribe(ids[i])) ) {
			fprintf(stderr,"fcomUnsubscribe(0x%08"PRIx32") failed: %s\n", ids[i], fcomStrerror(st));
		}
	}
	rval = 0;

bail:
	if ( rval )
		fcom_recv_stats(stderr);
	fcom_exit();
	free(pb);
	if ( nif )
		fclose(nif);
	if ( nof )
		fclose(nof);
	return rval;
}
