TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

#=============================
# Build the IOC support library

LIBRARY_IOC       += fcom
PROD_HOST   += fcomrtst
PROD_HOST   += fcomxtst
PROD_HOST   += fcomitst
PROD_HOST   += fcget
PROD_IOC    += prototst
PROD_IOC    += fcometst

fcget_SRCS = fcget.c
fcget_LIBS = fcom udpCommBSD

fcomitst_SRCS = fcomitst.c
fcomitst_LIBS = fcom udpCommBSD

fcomrtst_SRCS = fcomrtst.c
fcomrtst_LIBS = fcom udpCommBSD

fcomxtst_SRCS = fcomxtst.c
fcomxtst_LIBS = fcom udpCommBSD

fcometst_SRCS = fcometst.c
fcometst_LIBS = fcom
fcometst_LIBS_DEFAULT = udpCommBSD
fcometst_LIBS_RTEMS   = udpComm

prototst_SRCS_DEFAULT += prototst.c fcom_proto_xdr.c
prototst_LIBS         = fcom
prototst_LIBS_DEFAULT = udpCommBSD
prototst_LIBS_RTEMS   = udpComm

# Compile and add the code to the support library
fcom_SRCS += fc_init.c fc_strerror.c
fcom_SRCS += blobio.c

fcom_SRCS += fc_send.c xdr_enc.c
fcom_SRCS += fc_recv.c xdr_dec.c shtbl.c

USR_CPPFLAGS=  -DUSE_PTHREADS -DSUPPORT_SYNCGET -DSUPPORT_SETS
# note that for profiling fc_recv.c lanIpBasic must also
# be built with profiling enabled
#USR_CPPFLAGS+= -DENABLE_PROFILE

DBD                     += fcomIocshSupport.dbd
LIBRARY_IOC             += fcomIocshSupport
fcomIocshSupport_SRCS   += fcom_iocsh.c

# Hack: OP_SYS_LDLIBS on RTEMS still contains rtemsCom - get rid of it...
ifeq ($(OS_CLASS),RTEMS)
OP_SYS_LDLIBS=
endif

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
fc_recv$(OBJ):config.h

config.h:
	$(RM) $@
	$(RM) conftst.c	conftst.log
	echo '#include <pthread.h>' >>conftst.c
	echo 'int blah() { return pthread_mutexattr_setprotocol(0, PTHREAD_PRIO_INHERIT); }' >> conftst.c
	if $(COMPILE.c) conftst.c > conftst.log 2>&1 ; then echo '#define HAVE_PTHREAD_PRIO_INHERIT' >> $@ ; else echo '#undef HAVE_PTHREAD_PRIO_INHERIT' >> $@; fi

