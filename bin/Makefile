
# find rootdir:
ROOTDIR := $(shell until [ -f .ROOTDIR ];do cd ..;done;pwd)

include $(ROOTDIR)/Makefile.include
VPATH+=$(shell until [ -f .SOURCEDIR ];do cd ..;done;pwd)

#
# if we are in SOURCEDIR change to osdir and call make again:
# 
ifeq ($(shell [ -f .SOURCEDIR ]&&echo 1),1)
.EXPORT_ALL_VARIABLES:


$(ROOTDIR)/Makefile Makefile:
	@echo Makefile $@ checked

$(OSSID):
	mkdir -p $(OSSID)
	ln -s "../Makefile" $(OSSID)/Makefile

%:	$(OSSID)
	make -C $(OSSID) $(MAKEFLAGS) $@
else

unlock:	unlock.c $(LIB)/libpshal.a
	gcc $< -I$(INC) -I.. -L$(LIB) -lpshal -o $@

tracemcp:tracemcp.c $(LIB)/libpshal.a $(LIB)/libpvar.a
	gcc $< -g -I$(INC) -I.. -L$(LIB) -lpvar -lpshal -o $@

#psroute:psroute.c $(LIB)/libpshal.a $(LIB)/libarg.a
#	gcc $< -I$(INC) -I.. -L$(LIB) -larg -lpshal -lm -o $@

psconfig:psconfig.c $(LIB)/libpshal.a $(LIB)/libarg.a
	gcc $< -I$(INC) -I.. -L$(LIB) -larg -lpshal -lm -o $@
	chmod og+rx $@

psroute:psroute.c $(LIB)/libpshal.a $(LIB)/libarg.a
	gcc $< -I$(INC) -I.. -L$(LIB) -larg -lpshal -lm -o $@

%:	%.c
	gcc $< -I$(INC) -I.. -L$(LIB) -lpshal -o $@


endif



















