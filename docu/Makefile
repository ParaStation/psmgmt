SGML_CATALOG = /var/lib/sgml/CATALOG.db42xml
XSLT = xsltproc
XSLTFLAGS = --catalogs --timing --nonet

TARGETS = adminguide userguide

ADMINMAN1 = mlisten.1 psiadmin.1 test_nodes.1
ADMINMAN5 = parastation.conf.5
ADMINMAN8 = psid.8 psld.8 psroute.8 psscan.8
ADMINMANS = $(ADMINMAN1) $(ADMINMAN5) $(ADMINMAN8)

ADMINDEPS = intro.sgml intro_admin.sgml install.sgml config.sgml glossary.sgml
ADMINDEPS += $(patsubst %.1,%.sgml,$(ADMINMAN1))
ADMINDEPS += $(patsubst %.5,%.sgml,$(ADMINMAN5))
ADMINDEPS += $(patsubst %.8,%.sgml,$(ADMINMAN8)) boolargs.sgml

USERMAN1 = mpirun.1 psmstart.1
USERMAN7 = ps_environment.7 spawning.7
USERMANS = $(USERMAN1) $(USERMAN7)

USERDEPS = intro.sgml intro_user.sgml 
USERDEPS += startup.sgml using_mpi.sgml building.sgml glossary.sgml
USERDEPS += $(patsubst %.1,%.sgml,$(USERMAN1))
USERDEPS += $(patsubst %.7,%.sgml,$(USERMAN7))

all: validate html man pdf

.PHONY: validate html man pdf

validate: 
	@for doc in $(patsubst %,%.xml,$(TARGETS)); do \
	    res=`db2x.sh -o -W $$doc`; \
	    if [ "$$res" != $$doc' is valid.' ]; then \
		exit 1; \
	    else \
		echo $$res; \
	    fi \
	done

#
# Rules to create the HTML stuff
#
html: validate $(patsubst %,%/index.html,$(TARGETS))

%/index.html: %.xml
	@if [ ! -d $(@D) ]; then mkdir $(@D); fi; \
	cd $(@D); \
	export SGML_CATALOG=$(SGML_CATALOG); \
	$(XSLT) $(XSLTFLAGS) ../StyleSheets/html.xsl ../$<; \
	cd ..

adminguide/index.html: $(ADMINDEPS)

userguide/index.html: $(USERDEPS)

#
# Rules to create manual pages
#
man: validate $(patsubst %,man/%,$(ADMINMANS)) $(patsubst %,man/%,$(USERMANS))

$(patsubst %,man/%,$(ADMINMANS)): adminguide.xml
	@if [ ! -d $(@D) ]; then mkdir $(@D); fi; \
	cd $(@D); \
	export SGML_CATALOG=$(SGML_CATALOG); \
	$(XSLT) $(XSLTFLAGS) ../StyleSheets/db2man.xsl ../$<; \
	cd ..

$(patsubst %,man/%,$(ADMINMANS)): $(ADMINDEPS)

$(patsubst %,man/%,$(USERMANS)): userguide.xml
	@if [ ! -d $(@D) ]; then mkdir $(@D); fi; \
	cd $(@D); \
	export SGML_CATALOG=$(SGML_CATALOG); \
	$(XSLT) $(XSLTFLAGS) ../StyleSheets/db2man.xsl ../$<; \
	cd ..

$(patsubst %,man/%,$(USERMANS)): $(USERDEPS)

#
# Rules to create the PDF files
#
pdf: validate $(patsubst %,pdf/%.pdf,$(TARGETS))

# First create the tex file
pdf/%.tex: %.xml docu.dsl
	@if [ ! -d $(@D) ]; then mkdir $(@D); fi; \
	db2tex -o -s docu.dsl $<; \
	mv $(@F) $@

pdf/%.pdf: pdf/%.tex
	@cd $(@D); \
	pdfjadetex $(<F); \
	if egrep '^LaTeX Warning: There were undefined references.' \
					$(*F).log > /dev/null 2>&1; then \
	    pdfjadetex $(<F); \
	    pdfjadetex $(<F); \
	fi; \
	cd ..

pdf/adminguide.tex: $(ADMINDEPS)

pdf/userguide.tex: $(USERDEPS)

#
# General rules
#
.PHONY: clean realclean

clean:
	$(RM) *~

realclean: clean
	$(RM) -r $(TARGETS) man pdf
