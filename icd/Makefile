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

../%.pdf: ../%.tex fcom_api.h
	cd ..; pdflatex $(^:../%=%); pdflatex $(^:../%=%)

ifndef T_A
# EPICS Makefile system quirk; they should use :: everywhere...
realclean::
	$(RM) $(DOCS)
endif
