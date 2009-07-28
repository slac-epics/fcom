/* $Id$ */

/* Helper to convert FCOM error codes into ASCII strings
 * 
 * Author: Till Straumann <strauman@slac.stanford.edu>, 2009.
 */
#define __INSIDE_FCOM__
#include <fcom_api.h>
#include <errno.h>
#include <string.h>

static const char *unkn = "unknown FCOM error";

/* FCOM errors (all known FCOM_ERR_xyz EXCEPT for FCOM_ERR_SYS()) */
static const char *errstrs[]={
	"invalid FCOM ID",
	"no space (FCOM)",
	"invalid FCOM type",
	"invalid element count (FCOM)",
	"internal FCOM error",
	"ID not subscribed to FCOM",
	"FCOM ID not found",
	"invalid/unsupported FCOM version",
	"no memory or buffer (FCOM)",
	"invalid argument (FCOM)",
	"no data received (FCOM)",
	"trying to use unsupported FCOM feature",
	"FCOM timeout",
};


const char *
fcomStrerror(int err)
{
    if ( err >= 0 ) {
		if ( err > 0 )
			return "No error (FCOM) -- but return-value > 0";
		return "No error (FCOM)";
	}
	/* Is this originally a system error ? */
	if ( FCOM_ERR_IS_SYS(err) ) {
		/* Yes; extract the original 'errno' value */
		if ( (err = FCOM_ERR_SYS_ERRNO(err)) ) {
			/* and convert to string */
			return strerror(err);
		}
		/* have no original nonzero 'errno' -- don't know what happened */
		return "Unknown system error (FCOM)";
	}
	/* convert -1 based FCOM error status in positive, 0-based index */
	err = -(err+1);
	/* If out of range, return 'unknown error' */
	if ( err < 0 || err >= sizeof(errstrs)/sizeof(errstrs[0]) )
		return unkn;
	/* OK, obtain message from table above */
	return errstrs[err];
}
