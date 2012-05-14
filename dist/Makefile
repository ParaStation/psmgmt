#                                                       -*- Makefile -*-
# ParaStation
#
# Copyright (C) 2003 ParTec AG, Karlsruhe
# Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Authors:      Jens Hauke <hauke@par-tec.com>
#

TOP_SRCDIR?= ..
CONFIGARGS?=

# Makefile.sources have to define $(PKG_NAME) and $(SOURCE_FILES)
include $(TOP_SRCDIR)/dist/Makefile.sources
include $(TOP_SRCDIR)/Makefile.version
######################################################

all: rpm


DISTNAME=$(PKG_NAME)-$(VERSION_$(PKG_NAME))-$(RELEASE_$(PKG_NAME))

$(patsubst %,$(DISTNAME)/%,$(SOURCE_FILES)): INSTALL_FLAGS+=-m 644

$(DISTNAME)/%: $(TOP_SRCDIR)/%
	install $(INSTALL_FLAGS) -D $< $@

$(DISTNAME):
	install -d $@

$(DISTNAME)/$(DISTNAME).spec: $(TOP_SRCDIR)/dist/$(PKG_NAME).spec.templ $(DISTNAME)
	sed ":t \
		s,@VERSION_$(PKG_NAME)@,$(VERSION_$(PKG_NAME)),;t t \
		s,@RELEASE_$(PKG_NAME)@,$(RELEASE_$(PKG_NAME)),;t t" $< > $@

$(DISTNAME).tar.gz: $(patsubst %,$(DISTNAME)/%,$(SOURCE_FILES)) \
    $(patsubst %,$(DISTNAME)/%,$(SCRIPT_FILES)) $(DISTNAME)/$(DISTNAME).spec
	tar --owner=0 --group=0 -czf $@ $(DISTNAME)


.PHONY: tar
tar: $(DISTNAME).tar.gz

$(DISTNAME).src.rpm: $(DISTNAME)/$(DISTNAME).spec $(DISTNAME).tar.gz
	rpmbuild -bs --define="_sourcedir $$PWD" --define="_srcrpmdir $$PWD" $<

.PHONY: srpm
srpm: $(DISTNAME).src.rpm


RPMBUILDLOG=$(DISTNAME)$(shell echo $(CONFIGARGS)|tr " " "_").rpmbuild.log

.PHONY: rpm
rpm: $(DISTNAME)/$(DISTNAME).spec $(DISTNAME).tar.gz
	BUILDDIR=/var/tmp/$(DISTNAME)-$(USER); \
	test -e "$$BUILDDIR" -a ! -d "$$BUILDDIR" && $(RM) -r $$BUILDDIR; \
	mkdir $$BUILDDIR; \
	rpmbuild --clean $(TARGETARGS) \
		-bb --define="_sourcedir $$PWD" --define "_rpmdir $$PWD" \
		--define="_rpmfilename %%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm" \
		--define="_builddir $$BUILDDIR" $(CONFIGARGS) \
		$< | tee $(RPMBUILDLOG); \
	rmdir $$BUILDDIR
	@echo "RPM build log: $(RPMBUILDLOG)"

######################################################

SVN_VERSION = $(shell cd $(TOP_SRCDIR) && svnversion -n | tr -c [:alnum:] "_")

version:
	echo -e "s/RELEASE_\$$(PKG_NAME)=.*/RELEASE_\$$(PKG_NAME)=0.0.r$(SVN_VERSION)/\nwq" | ed -s $(TOP_SRCDIR)/Makefile.version


# xmlstarlet sel -C -t -o 'SVN_URL=' -v '//url' -n -o 'SVN_ROOT=' -v '//root'

tag:
	@{									\
		eval $$(svn info --xml $(TOP_SRCDIR)|xsltproc $(TOP_SRCDIR)/dist/svninfo.xsl -);\
										\
		if echo "$(SVN_VERSION)" | grep -vqe '^[0-9]\+$$'; then		\
	    		echo "Workingcopy not clean ($(SVN_VERSION))";		\
			exit 1;							\
		fi;								\
										\
		echo "Tagging r$(SVN_VERSION) as $(DISTNAME)";			\
										\
		if svn ls $${SVN_ROOT}/tags/$(PKG_NAME)/$(DISTNAME) > /dev/null 2>&1;then\
			echo "$${SVN_ROOT}/tags/$(PKG_NAME)/$(DISTNAME) already there";\
			exit 1;							\
		fi;								\
										\
		msg="Tagging $(DISTNAME).";					\
		svn copy -r$(SVN_VERSION) $${SVN_URL} $${SVN_ROOT}/tags/$(PKG_NAME)/$(DISTNAME) -m "$${msg}";\
		svn rm $${SVN_ROOT}/tags/$(PKG_NAME)/latest -m "$${msg}";	\
		svn copy $${SVN_ROOT}/tags/$(PKG_NAME)/$(DISTNAME) $${SVN_ROOT}/tags/$(PKG_NAME)/latest -m "$${msg}";\
	}

######################################################

DISTCLEAN+= $(PKG_NAME)*.tar.gz
DISTCLEAN+= $(PKG_NAME)*.rpm
DISTCLEAN+= $(PKG_NAME)*.spec
DISTCLEAN+= $(PKG_NAME)*.rpmbuild.log

######################################################
.PHONY: clean distclean
clean:
	$(RM) -r $(DISTNAME)

distclean: clean
	$(RM) $(DISTCLEAN)
