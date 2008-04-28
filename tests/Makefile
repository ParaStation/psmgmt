MPICC = /opt/parastation/mpi2/bin/mpicc

CFLAGS = -Wall

TARGETS = mpi_hello

all: $(TARGETS)

mpi_hello: CC = ${MPICC}

.PHONY: clean realclean

clean:
	rm -f *.o *~

realclean:
	rm -f *.o *~ $(TARGETS)
