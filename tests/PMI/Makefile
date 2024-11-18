CFLAGS = -Wall

TARGETS = reproducer pmi_hello abort

all: $(TARGETS)

PMI_LIB_OBJS = simple_pmi.o simple_pmiutil.o
PMI_LIB = libpmi.a

$(TARGETS): $(PMI_LIB)

$(PMI_LIB): $(PMI_LIB_OBJS)
	ar ru $@ $^
	ranlib $@

$(PMI_LIB_OBJS): pmi.h simple_pmiutil.h


.PHONY: clean realclean

clean:
	rm -f *.o *~

realclean:
	rm -f *.o *~ $(TARGETS) $(PMI_LIB)
