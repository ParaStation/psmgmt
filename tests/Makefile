#                                                       -*- Makefile -*-
# ParaStation
#
# Copyright (C) 2008 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Authors:      Norbert Eicker <eicker@par-tec.com>
#

MPICC1 = /opt/parastation/mpich/bin/mpicc
MPICC2 = /opt/parastation/mpi2/bin/mpicc

CFLAGS = -Wall

MPI1_TARGETS = mpi_hello.1 mpi_startup.1
MPI2_TARGETS = mpi_hello.2 mpi_startup.2

TARGETS = $(MPI2_TARGETS)

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
