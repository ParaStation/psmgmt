# CXX = g++
# CXXFLAGS = -Wall -g

CXX = icc
CXXFLAGS = -g
CPPFLAGS = -I/usr/include/libxml2
TARGETS = test_environment test_jobs

all: $(TARGETS)

test_environment: environment.o

test_jobs: LOADLIBES += -lxml2
test_jobs: jobs.o

.PHONY: clean realclean

clean:
	rm -f *~ *.o

realclean: clean
	rm -f $(TARGETS) core
