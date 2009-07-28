TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

#=============================
# Build the IOC support library

SRC_DIRS+=$(TOP)/src/udpComm/

LIBRARY_IOC += fcomBSD
PROD_Linux  += fcomrtst
PROD_Linux  += fcomxtst
PROD_IOC    += prototst


fcomrtst_SRCS = fcomrtst.c
fcomrtst_LIBS = fcomBSD

fcomxtst_SRCS = fcomxtst.c
fcomxtst_LIBS = fcomBSD

prototst_SRCS += prototst.c fcom_proto_xdr.c
prototst_LIBS = fcomBSD

# Compile and add the code to the support library
fcomBSD_SRCS += fc_init.c fc_strerror.c
fcomBSD_SRCS += blobio.c

fcomBSD_SRCS += fc_send.c xdr_enc.c
fcomBSD_SRCS += fc_recv.c xdr_dec.c shtbl.c
fcomBSD_SRCS += udpCommBSD.c

USR_CPPFLAGS=-DBSDSOCKET -DUSE_PTHREADS -DSUPPORT_SYNCGET

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
fc_recv.o:config.h

config.h:
	$(RM) $@
	$(RM) conftst.c	conftst.log
	echo '#include <pthread.h>' >>conftst.c
	echo 'int blah() { return pthread_mutexattr_setprotocol(0, PTHREAD_PRIO_INHERIT); }' >> conftst.c
	if $(COMPILE.c) conftst.c > conftst.log 2>&1 ; then echo '#define HAVE_PTHREAD_PRIO_INHERIT' >> $@ ; else echo '#undef HAVE_PTHREAD_PRIO_INHERIT' >> $@; fi

# That's a bad hack
inc: $(TOP)/src/udpComm

$(TOP)/src/udpComm:
	@echo "*** You need to check out the 'udpComm' module from the SSRL repository into 'src'***"; exit 1
