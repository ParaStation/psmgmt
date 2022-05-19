CFLAGS = -Wall

TARGETS = reproducer pmi_hello


all: $(TARGETS)

PMI_CLIENT_LIB = simple_pmi.o simple_pmiutil.o

reproducer: reproducer.o $(PMI_CLIENT_LIB)

pmi_hello: pmi_hello.o $(PMI_CLIENT_LIB)

#reproducer.o: pmi.h reproducer.c

$(PMI_CLIENT_LIB): pmi.h simple_pmiutil.h

.PHONY: clean realclean

clean:
	rm -f *.o *~

realclean:
	rm -f *.o *~ $(TARGETS)
