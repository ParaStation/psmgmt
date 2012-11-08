#
# ParaStation
#
# Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Thomas Moschny <moschny@par-tec.com>
#

include $(top_srcdir)/common.am

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = include lib bin plugins doc scripts

if TESTPLUGINS_ENABLED
  SUBDIRS += tests/plugins tests/functional
endif

EXTRA_DIST = README tests/psid
EXTRA_DIST += dist/.gitignore dist/Makefile dist/Makefile.sources	\
	dist/psmgmt.spec.templ

dist_doc_DATA = LICENSE.QPL NEWS ChangeLog

PKG_VER := $(shell ${top_srcdir}/scripts/vcversion -r $(top_srcdir) -n)

# ---- Version ----
#
nodist_doc_DATA = VERSION.psmgmt

.PHONY: VERSION.psmgmt
VERSION.psmgmt:
	@echo "$(PACKAGE_NAME) $(PKG_VER) ($(shell date))" > $@

CLEANFILES = VERSION.psmgmt

# ---- VERSION ----
#
dist-hook:
	@echo "$(PKG_VER)" > $(top_distdir)/VERSION

# ---- proxy to dist/Makefile
#
.PHONY: tar rpm srpm tag version
tar rpm srpm tag version:
	TOP_SRCDIR=$(abs_top_srcdir) $(MAKE) -C dist $@

DISTCLEANFILES = dist/Makefile
