//////////////////////////////////////////////////////////////////////////////
// This file is part of 'fcom'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'fcom', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
#include <iocsh.h>
#include <registry.h>
#include <epicsExport.h>
#include <stdio.h>

#include <fcom_api.h>

static const struct iocshArg _fcomInitArgs[] = {
	{
	"MC prefix <ip[:port]>",
	iocshArgString
	},
	{
	"num RX buffers",
	iocshArgInt
	}
};

static const struct iocshArg *_fcomInitArgsp[] = {
	&_fcomInitArgs[0],
	&_fcomInitArgs[1],
	0
};

struct iocshFuncDef _fcomInitDesc = {
	"fcomInit",
	2,
	_fcomInitArgsp
};

static void
_fcomInitFunc(const iocshArgBuf *args)
{
	fcomInit(args[0].sval, args[1].ival);
}

static const struct iocshArg *_fcomDumpStatsArgsp[] = {
	0
};

struct iocshFuncDef _fcomDumpStatsDesc = {
	"fcomDumpStats",
	0,
	_fcomDumpStatsArgsp
};

static void
_fcomDumpStatsFunc(const iocshArgBuf *args)
{
	fcomDumpStats(stdout);
}

static const struct iocshArg _fcomStrerrorArgs[] = {
	{
	"error code",
	iocshArgInt
	},
};


static const struct iocshArg *_fcomStrerrorArgsp[] = {
	&_fcomStrerrorArgs[0],
	0
};

struct iocshFuncDef _fcomStrerrorDesc = {
	"fcomStrerror",
	1,
	_fcomStrerrorArgsp
};

static void
_fcomStrerrorFunc(const iocshArgBuf *args)
{
	fprintf(stderr,"%s\n",fcomStrerror(args[0].ival));
}


static void
fcomRegistrar(void)
{
	iocshRegister(&_fcomInitDesc,      _fcomInitFunc);
	iocshRegister(&_fcomDumpStatsDesc, _fcomDumpStatsFunc);
	iocshRegister(&_fcomStrerrorDesc,  _fcomStrerrorFunc);
}

epicsExportRegistrar(fcomRegistrar);
