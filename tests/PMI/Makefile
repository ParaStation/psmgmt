CFLAGS = -Wall

TARGETS = reproducer


all: $(TARGETS)

reproducer: reproducer.o simple_pmi.o simple_pmiutil.o

reproducer.o: pmi.h reproducer.c

simple_pmi.o simple_pmiutil.o: pmi.h simple_pmiutil.h

.PHONY: clean realclean

clean:
	rm -f *.o *~

realclean:
	rm -f *.o *~ $(TARGETS)
