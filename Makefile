#############################################################################
#      @(#)makefile    2.00 (Karlsruhe) 2000
#
#      27.06.2000   Jens Hauke
#      23.08.2000
#############################################################################
#Create Only mcp's:
#>make mcp
#Create mcp pshal and kernelmodule:
#>make all
#Create pshal and kernelmodule:
#>make allbutmcp
#or simple
#>make
#
#On Linux use KDIR to set Kerneldir and optionalOSSUF for extra info like SMP
#>make KDIR=/usr/src/linux-2.2.16 OSSUF=_SMP
#
#On Problems with .kernelrelease call "make include/linux/version.h" as
# root inside Kerneldir




.EXPORT_ALL_VARIABLES:


ROOTDIR := $(shell until [ -f .ROOTDIR ];do cd ..;done;pwd)
#$(shell if [ "$$PWD" != "" ]; then echo $$PWD; else pwd; fi)
PSMDIR := $(ROOTDIR)/kern
ZLIBDIR := $(ROOTDIR)/zlib
PSHALDIR := $(ROOTDIR)/pshal
PSPORTDIR := $(ROOTDIR)/psport
MCPDIR := $(ROOTDIR)/kern/mcp
INCLUDEDIR := $(ROOTDIR)/include
OSNAME := $(shell uname -s)

ifeq ($(OSNAME),Linux)
  ifeq ($(KDIR),)
OSVERSION := $(shell uname -r)
KERNEL_DIR := /usr/src/linux
  else
KERNEL_DIR := $(KDIR)
#OSVERSION := $(shell cat $(KDIR)/.kernelrelease)$(OSSUF)
OSVERSION := $(shell cat $(KDIR)/include/linux/version.h|grep UTS_RELEASE|cut -f 3 -d" ")$(OSSUF)
  endif
else
OSVERSION := $(shell uname -r)
endif

OSARCHITECTUR :=  $(shell uname -m)
OSID  := $(OSARCHITECTUR)_$(OSNAME)_$(OSVERSION)
OSSID := $(OSARCHITECTUR)_$(OSNAME)$(OSSUF)


MODULEARCHIVEDIR := $(ROOTDIR)/modules/$(OSID)
LIBARCHIVEDIR := $(ROOTDIR)/lib/$(OSSID)
TMPOBJDIR := $(ROOTDIR)/tmp

LIB := $(LIBARCHIVEDIR)
INC := $(INCLUDEDIR)
GCCINC := -I$(INC) -I$(PSMDIR)/$(OSNAME)

ifeq ($(shell cd .;pwd),$(ROOTDIR))

allbutmcp:	dep psm pshal psport pvar arg

all:	mcpdep mcp allbutmcp

mcp:	mcpdep
	make -C $(MCPDIR) all

psm:
	make -C $(PSMDIR) psm

#xxx:
#	echo "${OSVERSION}"

pshal:	FORCE	
	make -C $(PSHALDIR) pshal

psport:	FORCE	
	make -C $(PSPORTDIR) psport

pvar:	FORCE	
	make -C $(PSHALDIR) pvar

arg:	FORCE
	make -C $(PSHALDIR) arg

mcpdep:
	make -C $(MCPDIR) dep

mcpclean:
	make -C $(MCPDIR) clean

dep:
	make -C $(PSMDIR) $@
	make -C $(PSHALDIR) $@
	make -C $(PSPORTDIR) $@

clean:
	make -C $(PSMDIR) $@
	make -C $(PSHALDIR) $@
	make -C $(PSPORTDIR) $@
	rm -rf tmp/*

TAGS:
	ctags -e *.[ch] */*.[ch] */*/*.[ch]


tags:	TAGS

etags:	TAGS

FORCE:


getenv:
	echo ddd:$(MAKEFLAGS)
	$(COMMAND)	

_ROOTFILEEXEC := 1

endif



