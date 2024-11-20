CFLAGS = -Wall

TARGETS =  getSizes putGet log spawn abort spawn_jobsize spawn_recursive

all: $(TARGETS)

$(TARGETS): %: %.o

$(TARGETS): LOADLIBES+=-lpmix

.PHONY: clean realclean

clean:
	rm -f *.o *~

realclean:
	rm -f *.o *~ $(TARGETS)
