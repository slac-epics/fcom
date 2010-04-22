/* $Id: fcomxtst.c,v 1.2 2009/07/28 19:46:56 strauman Exp $ */

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
char        *prefix;

	if ( ! (prefix = getenv("FCOM_MC_PREFIX")) )
		prefix = "239.255.0.0";

	if ( (st = fcomInit(prefix, 0)) ) {
		fprintf(stderr, "fcomInit() failed: %s\n", fcomStrerror(st));
		goto bail;
	}

	nmb          = 0;
	pbl->fc_idnt = 0;

	while ( (err = fcom_get_blob_from_file(infile, pb, BLOBSZ)) > 0 ) {

		if ( FCOM_GET_GID(pb->fc_idnt) != FCOM_GET_GID(pbl->fc_idnt) ) {

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

	if ( err < 0 ) {
		fprintf(stderr,"get_blob_from_file failed (check file syntax)\n");
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
