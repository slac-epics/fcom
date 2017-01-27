TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

#=============================
# Build the IOC support library

INC=fcom_api.h

DOCS += fcom_icd.pdf

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

../%.pdf: ../%.tex ../fcom_api.h
	if sha1sum --version > /dev/null ; then \
	if [ ! -f $(@:%.pdf=%.cksum) ] ||  ! sha1sum --status -c $(@:%.pdf=%.cksum) ; then \
		(cd ..; pdflatex $(@:../%.pdf=%.tex); pdflatex $(@:../%.pdf=%.tex)); \
		sha1sum $@ $(@:%.pdf=%.tex) ../fcom_api.h > $(@:%.pdf=%.cksum); \
	else \
		touch $@ ;\
	fi \
	else \
		echo "WARNING: 'sha1sum' not found -- unable to check if $@ is up-to-date" ; \
	fi

ifndef T_A
realclean:
	$(RM) $(DOCS)
endif

.PRECIOUS: ../fcom_icd.pdf
