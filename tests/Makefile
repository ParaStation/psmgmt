###################################################### -*- Makefile -*- #####
#                  ParaStation
#        Copyright (c) 2008 ParTec Cluster Competence Center GmbH, Munich
#############################################################################
#
# $Id$
#
# @author
#         Norbert Eicker <eicker@par-tec.de>
#############################################################################

MPICC1 = /opt/parastation/mpich/bin/mpicc
MPICC2 = /opt/parastation/mpi2/bin/mpicc

CFLAGS = -Wall

TARGETS = mpi_hello.1 mpi_hello.2 mpi_startup.1 mpi_startup.2

all: $(TARGETS)

%.1: %.c
	$(MPICC1) $(CFLAGS) $(CPPFLAGS) -o $@ $<

%.2: %.c
	$(MPICC2) $(CFLAGS) $(CPPFLAGS) -o $@ $<


.PHONY: clean realclean

clean:
	rm -f *.o *~

realclean:
	rm -f *.o *~ $(TARGETS)
