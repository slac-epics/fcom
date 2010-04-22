/* $Id: fcomitst.c,v 1.1 2010/03/18 18:21:59 strauman Exp $ */
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#include <getopt.h>

#include <fcom_api.h>
#include <fcomP.h>

#define IFD 0

typedef struct IdRec_ {
	FcomID         id;
	int            refcnt;
	struct IdRec_ *next;
} IdRec, *Id;

static Id ids  = 0;

static Id frid = 0;

static Id
idalloc(FcomID id)
{
Id rval;
	if ( frid ) {
		rval          = frid;
		frid          = frid->next;
	} else {
		if ( !(rval = malloc(sizeof(*rval))) )
			return 0;
	}
	rval->id      = id;
	rval->refcnt  = 1;
	rval->next    = 0;

	return rval;
}

static void
idfree(Id id)
{
	id->next = frid;
	frid     = id;
}

static Id *
lfnd(FcomID id)
{
Id *p;
	for ( p=&ids; *p; p=&(*p)->next ) {
		if ( (*p)->id == id )
			return p;
	}
	return 0;
}

static void
lswp(Id p, Id n)
{
IdRec tmp;
	tmp     = *p;
	n->next = n;
	*p      = *n;
	*n      = tmp;
}

static void
lins(FcomID id)
{
Id     *p,t;

	if ( ! (t=idalloc(id)) ) {
		fprintf(stderr,"Fatal error - no memory\n");
		abort();
	}

	for ( p=&ids; *p;  p=&(*p)->next ) {
		if ( (*p)->id == t->id ) {
			(*p)->refcnt++;
			idfree(t);
			return;
		} else if ( (*p)->id > t->id ) {
			lswp(*p,t);
			return;
		}
	}
	t->next = 0;
	*p      = t;
}

void
ldel(Id *p)
{
Id t;
	if ( (t = *p) && --t->refcnt <= 0 ) {
		*p      = t->next;
		t->next = 0;
		idfree(t);
	}
}

static int
gch(char *p_ch)
{
int            rval = 1;
struct termios oatt;
struct termios natt;

	if ( tcgetattr(IFD, &oatt) ) {
		perror("tcgetattr(oatt)");
		return 1;
	}

	natt = oatt;

	cfmakeraw(&natt);

	if ( tcsetattr(IFD, TCSAFLUSH, &natt) ) {
		perror("tcsetattr(natt)");
		goto bail;
	}

	if ( 1 != read(IFD, p_ch, 1) ) {
		perror("read 1 char");
		goto bail;
	}

	rval = 0;

bail:
	if ( tcsetattr(IFD, TCSAFLUSH, &oatt) ) {
		perror("tcsetattr(oatt) -- restoring original attributes");
	}
	return rval;
}

static int
fccall(const char *prefix, int status)
{
	if ( status ) {
		fprintf(stderr,"%s failed: %s\n", prefix, fcomStrerror(status));
	}
	return status;
}

#define FCCALL(func,args...) fccall(#func,func(args))

static void
help()
{
	printf("Menu----\n");
	printf("  (q)uit\n");
	printf("  (s)ubscribe\n");
	printf("  (u)nsubscribe\n");
	printf("  (g)et blob (synchronous)\n");
	printf("  (a)synchronous get blob\n");
	printf("  (l)ist subscribed IDs\n");
	printf("  (d)ump stats\n");
}

static int
menu(void)
{
char        ch,ch1;
int         rval;
int64_t     v;
static FcomID id = FCOM_ID_NONE;
FcomBlobRef pb;
char        buf[20];
unsigned    tout = 0;

	printf("\nCommand (s/u/g/a/l/d/h/q) ?");
	fflush(stdout);
	if ( (rval = gch(&ch)) ) {
		printf("READ ERROR\n");
	} else {
		fputc(ch, stdout);
		switch (ch) {
			case 's':
			case 'u':
				id = FCOM_ID_NONE;

			case 'g':
			case 'a':
				do {
					printf("\n ID? ");
					fflush(stdout);

					if ( ! fgets(buf,sizeof(buf),stdin) )
						return -1;

					if ( '\n' != buf[0] && !isdigit(buf[0]) ) {
						return -1;
					}
					if ( '\n' == buf[0] && FCOM_ID_NONE != id ) {
						v = id;
						break;
					}
				} while ( 1 != sscanf(buf,"%"SCNi64,&v) );
				id = v;
				printf("\n ID is: 0x%"PRIx32"\n", id);
			break;
			default:
			break;
		}
		switch ( ch ) {

			case 'h':
				help();
			break;

			case 's':
				if ( 0 == FCCALL(fcomSubscribe, id, FCOM_SYNC_GET) ) {
					lins(id);
				}
			break;

			case 'u':
				{
				Id *p;
				if ( ! (p = lfnd(id)) ) {
					printf("\n ID 0x%"PRIx32" NOT FOUND\n",id);
				} else {
					if ( 0 == FCCALL(fcomUnsubscribe, id) ) {
						ldel(p);
						id = FCOM_ID_NONE;
					}
				}
				
				}
			break;

			case 'g':
				tout = 2000;
				/* Fall thru */
			case 'a':
				if ( 0 == FCCALL(fcomGetBlob, id, &pb, tout) ) {
					FCCALL(fcom_put_blob_to_file,stdout, pb);
					if ( 0 != FCCALL(fcomReleaseBlob, &pb) ) {
						fprintf(stderr,"FATAL ERROR\n");
						abort();
					}
				}
			break;

			break;

			case 'q':
				printf("\nOK - do you really want to quit (type 'y') ?");
				fflush(stdout);
				ch1 = ' ';
				if ( 0 == gch(&ch1) && 'y' != ch1 )
					ch = 0;
				printf("%c\n",ch1);
			break;

			case 'l':
			{
				Id id;

				printf("\nList of currently subscribed IDs (with refcount):\n");
				for ( id=ids; id; id=id->next ) {
					printf("  0x%"PRIx32" (%3u)\n", id->id, id->refcnt);
				}
			}
			break;

			case 'd':
				fcomDumpStats(stdout);
			break;

			default:
				printf("UNKNOWN COMMAND\n");
			break;
		}
	}
	return rval ? -1: ch;
}

static void usage(const char *nm)
{
	fprintf(stderr,"Usage: %s [-p multicast prefix] [ID] [ID] ...\n", nm);
}

int
main(int argc, char **argv)
{
int        ch;
char       *prefix;
uint64_t   v;
int        i;
FcomID     id;

	if ( ! (prefix = getenv("FCOM_MC_PREFIX")) ) {
		prefix = "239.255.0.0";
	}

	while ( (ch = getopt(argc, argv, "hp:")) >= 0 ) {
		switch ( ch ) {
			case 'h': usage(argv[0]); return 0;
			case 'p': prefix = optarg; break;
			default:
				fprintf(stderr,"unknown option: %c\n",ch);
			return(1);
		}
	}
	if ( FCCALL(fcomInit,prefix,1000) )
		return 1;

	for ( i = optind; i<argc; i++ ) {
		if ( 1 == sscanf(argv[i],"%"SCNx64,&v) ) {
			id = v;
			if ( 0 == FCCALL(fcomSubscribe, id, FCOM_SYNC_GET) ) {
					lins(id);
			}
		}
	}

	do {
	} while ( 'q' != menu());

	return 0;
}
