###################################################### -*- Makefile -*- #####
#                  ParaStation
#        Copyright (c) 2002 ParTec AG
#        All rights reserved.
#############################################################################
#
# $Id: Makefile,v 1.1 2002/11/26 13:19:32 eicker Exp $
#
# @author
#         Norbert Eicker <eicker@par-tec.de>
#############################################################################

PSDIR = /opt/parastation
PSDIR = /home/eicker/Work/p3/build/dist/opt/parastation

CFLAGS = -Wall -g -I$(PSDIR)/include
LDFLAGS = -L$(PSDIR)/lib
LOADLIBES = -lpshal -lpsport -lpsi -lpse -lpthread

TARGETS= native


all: $(TARGETS)

native: native.o

.PHONY: clean realclean

clean:
	rm -f *~ *.o

realclean: clean
	rm -f $(TARGETS)
