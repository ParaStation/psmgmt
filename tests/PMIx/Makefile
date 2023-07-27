CFLAGS = -Wall

TARGETS =  getSizes putGet log

all: $(TARGETS)

$(TARGETS): %: %.o

$(TARGETS): LOADLIBES+=-lpmix

.PHONY: clean realclean

clean:
	rm -f *.o *~

realclean:
	rm -f *.o *~ $(TARGETS)
