
# find rootdir:
ROOTDIR := $(shell until [ -f .ROOTDIR ];do cd ..;done;pwd)

include $(ROOTDIR)/Makefile.include
VPATH+=$(shell until [ -f .SOURCEDIR ];do cd ..;done;pwd)

TOOLS:=psconfig pscounter test_nodes

#
# if we are in SOURCEDIR change to osdir and call make again:
# 
ifeq ($(shell [ -f .SOURCEDIR ]&&echo 1),1)
.EXPORT_ALL_VARIABLES:

$(ROOTDIR)/Makefile Makefile:
	@echo Makefile $@ checked

toolsstrip:

$(OSID):
	mkdir -p $(OSID)
	ln -s "../Makefile" $(OSID)/Makefile

%:	$(OSID)
	make -C $(OSID) $@
else

tools:	$(TOOLS)

unlock:	unlock.c $(LIB)/libpshal.a
	gcc $< -I$(INC) -I.. -L$(LIB) -lpshal -o $@

tracemcp:tracemcp.c $(LIB)/libpshal.a $(LIB)/libpvar.a
	gcc $< -g -I$(INC) -I.. -L$(LIB) -lpvar -lpshal -o $@

#psroute:psroute.c $(LIB)/libpshal.a $(LIB)/libarg.a
#	gcc $< -I$(INC) -I.. -L$(LIB) -larg -lpshal -lm -o $@

psconfig:psconfig.c $(LIB)/libpshal.a $(LIB)/libarg.a
	gcc $< -s -I$(INC) -I.. -L$(LIB) -larg -lpshal -lm -o $@
	chmod og+rx $@

test_nodes:test_nodes.c $(LIB)/libpshal.a $(LIB)/libarg.a
	gcc $< -s -I$(INC) -I.. -L$(LIB) -larg -lpshal -lpsport -lpse -lpsi -lpthread -lm -o $@
	chmod og+rx $@

test_pse:test_pse.c $(LIB)/libpshal.a $(LIB)/libarg.a $(LIB)/libpse.a  $(LIB)/libpsi.a
	gcc $< -s -I$(INC) -Wall -I.. -I$(INC)/../userspace/include -L$(LIB) -larg -lpshal -lpsport -lpse -lpsi -lpthread -lm -o $@

psroute:psroute.c $(LIB)/libpshal.a $(LIB)/libarg.a
	gcc $< -I$(INC) -I.. -L$(LIB) -larg -lpshal -lm -o $@

pscounter:pscounter.c $(LIB)/libpshal.a
	gcc $< -s -I$(INC) -I.. -L$(LIB) -lpshal -lm -o $@

psmrun:psmrun.c
	gcc $< -s -I$(INC) -I.. -L$(LIB) -Wall -lpshal -lpse -lpsi -o $@

%:	%.c
	gcc $< -I$(INC) -I.. -L$(LIB) -lpshal -o $@


endif



















