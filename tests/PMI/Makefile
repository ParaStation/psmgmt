CFLAGS = -Wall

TARGETS = reproducer


all: $(TARGETS)

reproducer: LOADLIBES += -lpmi

.PHONY: clean realclean

clean:
	rm -f *.o *~

realclean:
	rm -f *.o *~ $(TARGETS)
