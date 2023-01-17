CFLAGS = -Wall

TARGETS =  getSizes putGet

all: $(TARGETS)

$(TARGETS): %: %.o

$(TARGETS): LOADLIBES+=-lpmix

.PHONY: clean realclean

clean:
	rm -f *.o *~

realclean:
	rm -f *.o *~ $(TARGETS)
