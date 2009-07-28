/* $Id$ */

/* Test program for FCOM transmitter
 *
 * Author: Till Straumann <strauman@slac.stanford.edu>, 2009.
 *
 * Read blob definitions from stdin and assemble them into
 * FCOM groups. Subsequent blob definitions with identical GIDs
 * are assembled into groups. When a different GID is encountered
 * the previous group is terminated and sent off.
 * If a group has only a single member then fcomPutBlob() is used.
 */
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

#include <fcom_api.h>
#include <fcomP.h>

#define BLOBSZ 1000

static void
wrapgrp(FcomGroup g, FcomBlobRef pbl, int nmb)
{
int st;

	if ( nmb > 1 ) {
		if ( (st = fcomPutGroup(g)) ) {
			fprintf(stderr,"fcomPutGroup() failed: %s\n", fcomStrerror(st));
		}
	} else {
		if ( 1 == nmb ) {
			if ( (st=fcomPutBlob(pbl)) )
				fprintf(stderr,"fcomPutBlob() failed: %s\n", fcomStrerror(st));
		}
		fcomFreeGroup(g);
	}
}

int
main(int argc, char **argv)
{
int         st;
int         rval = 1;
FcomBlobRef pb   = malloc(BLOBSZ);
FcomBlobRef pbl  = malloc(BLOBSZ);
FcomBlobRef tmp;
int         err,nmb;
FILE        *infile = stdin;
FcomGroup   g = 0;

	if ( (st = fcomInit("239.255.0.0", 0)) ) {
		fprintf(stderr, "fcomInit() failed: %s\n", fcomStrerror(st));
		goto bail;
	}

	nmb                 = 0;
	pbl->fcb_v1.fc_idnt = 0;

	while ( (err = fcom_get_blob_from_file(infile, &pb->fcb_v1, BLOBSZ)) > 0 ) {

		if ( FCOM_GET_GID(pb->fcb_v1.fc_idnt) != FCOM_GET_GID(pbl->fcb_v1.fc_idnt) ) {

			wrapgrp(g, pbl, nmb);

			nmb = 0;

			if ( (st = fcomAllocGroup(FCOM_ID_ANY, &g)) ) {
				fprintf(stderr,"fcomAllocGroup() failed: %s\n", fcomStrerror(st));
				g = 0;
				goto bail;
			}
		}


		if ( (st = fcomAddGroup(g, pb)) ) {
				fprintf(stderr,"fcomAddGroup() failed: %s\n", fcomStrerror(st));
				goto bail;
		} else {
			nmb++;
		}
		/* switch buffers */
		tmp = pbl;
		pbl = pb;
		pb  = tmp;
	}

	wrapgrp(g, pbl, nmb);
	g    = 0;

	rval = 0;

bail:
	fcomFreeGroup(g);
	fcom_exit();
	free(pb);
	return rval;
}
