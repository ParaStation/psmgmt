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



ROOTDIR := $(shell until [ -f .ROOTDIR ];do cd ..;done;pwd)

include $(ROOTDIR)/Makefile.include



ifeq ($(shell cd .;pwd),$(ROOTDIR))

allbutmcp:	dep psm libs tools buildno

all:	mcpdep mcp allbutmcp buildno

libs:	pshal psport pvar arg libstrip

libstrip: $(LIBARCHIVEDIR)/*
	mkdir -p $(LIBARCHIVEDIR)_strip
	cp $^ $(LIBARCHIVEDIR)_strip/.
	strip -g $(LIBARCHIVEDIR)_strip/*

mcp:	mcpdep
	$(MAKE) -C $(MCPDIR) all

psm:
	$(MAKE) -C $(PSMDIR) psm

#xxx:
#	echo "${OSVERSION}"

pshal:	FORCE	
	$(MAKE) -C $(PSHALDIR) pshal

psport:	FORCE	
	$(MAKE) -C $(PSPORTDIR) psport

psport2:	FORCE	
	$(MAKE) -C $(PSPORT2DIR) psport2

pvar:	FORCE	
	$(MAKE) -C $(PSHALDIR) pvar

arg:	FORCE
	$(MAKE) -C $(PSHALDIR) arg

mcpdep:
	$(MAKE) -C $(MCPDIR) dep

mcpclean:
	$(MAKE) -C $(MCPDIR) clean

tools:	FORCE
	$(MAKE) -C $(TOOLDIR) tools

toolsstrip:	FORCE
	$(MAKE) -C $(TOOLDIR) toolsstrip

dep:
	$(MAKE) -C $(PSMDIR) $@
	$(MAKE) -C $(PSHALDIR) $@
	$(MAKE) -C $(PSPORTDIR) $@
	$(MAKE) -C $(PSPORT2DIR) $@

buildno:
	@echo "## Build #################################################"
	@cat include/.build
ifeq ($(DEVELOP),1)
	@echo development version
else
	@echo production version
endif
	@echo "##########################################################"

clean:
	$(MAKE) -C $(PSMDIR) $@
	$(MAKE) -C $(PSHALDIR) $@
	$(MAKE) -C $(PSPORTDIR) $@
	$(MAKE) -C $(PSPORT2DIR) $@
	rm -rf tmp/*

FORCE:


getenv:
	echo ddd:$(MAKEFLAGS)
	$(COMMAND)	

_ROOTFILEEXEC := 1

endif



