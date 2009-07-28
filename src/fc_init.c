/* $Id$ */

/* FCOM Initialization.
 *
 * The routine provided by this file mostly checks arguments
 * and then invokes fcom_recv_init() and fcom_send_init() which
 * are weak aliases so that an application may link the receiving
 * and sending parts individually.
 * This may be useful e.g., on test platforms where different
 * applications/processes send and receive (the RX port can only
 * be bound by ONE process).
 *
 * Author: Till Straumann <strauman@slac.stanford.edu>, 2009.
 */
#define __INSIDE_FCOM__
#include <fcom_api.h>
#include <fcomP.h>

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <netinet/in.h>
#include <arpa/inet.h>

uint32_t fcom_g_prefix = 0;

int      fcom_xsd      = -1;
int      fcom_rsd      = -1;

/* Tunable parameters */
int      fcom_port     = FCOM_PORT_DEFLT;
int      fcom_rx_priority_percent = 80;

static uint32_t m[] = {
	0xffff0000,
	0xff00ff00,
	0xf0f0f0f0,
	0xcccccccc,
	0xaaaaaaaa
};

int fcom_nzbits(uint32_t x)
{
int      i,j,v;

	for ( i=v=0, j=1<<(sizeof(m)/sizeof(m[0])-1); j; i++, j>>=1 ) {
		if ( x & m[i] ) {
			x &= m[i];
			v += j;
		}
	}
	if ( x )
		v++;
	
	return v;
}

int
fcomInit(const char *ip_group, unsigned n_bufs)
{
struct in_addr ina;
uint32_t       h; /* in host-order */
char           str[100];
char          *col;
int            err;

	if ( fcom_xsd >= 0 || fcom_rsd >= 0 ) {
		fprintf(stderr,"Warning: FCOM already initialized\n");
		return 0;
	}

	if ( !ip_group ) {
		fprintf(stderr,"Need a <mcast_prefix>[:<port>] argument\n");
		return FCOM_ERR_INVALID_ARG;
	}

	strncpy(str, ip_group, sizeof(str)-1);
	str[sizeof(str)-1] = 0;

	if ( (col = strchr(str, ':')) )
		*col++=0;

	if ( 0 == inet_pton(AF_INET, str, &ina) ) {
		fprintf(stderr,"fcomInit: invalid multicast prefix (string in dot notation)\n");
		fprintf(stderr,"Error: %s\n", strerror(errno));
		return FCOM_ERR_INVALID_ARG;
	}

	h = ntohl(ina.s_addr);

	if ( col ) {
		if ( 1 != sscanf(col,"%i",&fcom_port) ) {
			fprintf(stderr,"Unable to scan port number\n");
			return FCOM_ERR_INVALID_ARG;
		}
	}

	if ( (h & 0xe0000000) != 0xe0000000 ) {
		fprintf(stderr,"Not a valid multicast address\n");
		return FCOM_ERR_INVALID_ARG;
	}

	if ( ((1 << fcom_nzbits(FCOM_GID_MAX)) - 1) & h ) {
		fprintf(stderr,"Multicast prefix overlaps max. GID\n");
		return FCOM_ERR_INVALID_ARG;
	}

	fcom_g_prefix = ina.s_addr;

	/* Initialize RX and TX parts if they are linked
	 * (fcom_recv_init/fcom_send_init are weak symbols)
	 */

	/* create RX socket with known port first to reduce
	 * the odds that it might be used by the system when
	 * creating the TX socket.
	 */
	if ( fcom_recv_init && n_bufs > 0 ) {
		if ( (fcom_rsd = udpCommSocket(fcom_port)) < 0 ) {
			return FCOM_ERR_SYS(-fcom_rsd);
		}
		if ( ( err = fcom_recv_init(n_bufs)) ) {
			return err;
		}
	}

	if ( fcom_send_init ) {
		if ( (fcom_xsd = udpCommSocket(0)) < 0 ) {
			return FCOM_ERR_SYS(-fcom_xsd);
		}
		if ( (err = fcom_send_init()) ) {
			return err;
		}
	}

	return 0;
}

int
fcom_exit()
{
int rval;

	if ( fcom_send_fini ) {
		if ( (rval = fcom_send_fini()) ) {
			return rval;
		}
		if ( (rval = udpCommClose(fcom_xsd)) ) {
			return rval;
		}
	}

	if ( fcom_recv_fini ) {
		if ( (rval = fcom_recv_fini()) ) {
			return rval;
		}
		if ( (rval = udpCommClose(fcom_rsd)) ) {
			return rval;
		}
	}

	fcom_xsd      = -1;
	fcom_port     = FCOM_PORT_DEFLT;
	fcom_g_prefix = 0;

	return 0;
}

void
fcomDumpStats(FILE *f)
{
	if ( fcom_recv_stats )
		fcom_recv_stats(f);
	if ( fcom_send_stats )
		fcom_send_stats(f);
}

int
fcomGetState(int n_keys, uint32_t key_arr[], uint64_t value_arr[])
{
int i;
int rval;
	for ( i = 0; i<n_keys; i++ ) {
		if ( FCOM_STAT_IS_RX(key_arr[i]) && fcom_get_rx_stat ) {
			if ( (rval = fcom_get_rx_stat(key_arr[i], value_arr+i)) )
				return rval;
		} else if ( FCOM_STAT_IS_TX(key_arr[i]) && fcom_get_tx_stat ) {
			if ( (rval = fcom_get_tx_stat(key_arr[i], value_arr+i)) )
				return rval;
		} else {
			return FCOM_ERR_UNSUPP;
		}
	}
	return 0;
}
