###################################################### -*- Makefile -*- #####
#                  ParaStation
#        Copyright (c) 2002 ParTec AG
#        All rights reserved.
#############################################################################
#
# $Id: Makefile,v 1.2 2002/11/27 09:06:24 eicker Exp $
#
# @author
#         Norbert Eicker <eicker@par-tec.de>
#############################################################################

PSDIR = /opt/parastation

CPPFLAGS = -I$(PSDIR)/include
CFLAGS = -Wall -g
LDFLAGS = -L$(PSDIR)/lib
LOADLIBES = -lpse -lpsi -lpsport -lpshal -lpthread

TARGETS= native


all: $(TARGETS)

native: native.o

.PHONY: clean realclean

clean:
	rm -f *~ *.o

realclean: clean
	rm -f $(TARGETS)
