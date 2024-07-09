#
# ParaStation
#
# Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022-2024 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = include lib bin plugins doc scripts config

if !BUILD_WITHOUT_TESTPLUGINS
  SUBDIRS += tests/plugins tests/functional
endif

EXTRA_DIST = README tests/psid dist/.gitignore dist/Makefile.sources	\
	dist/psmgmt.spec.templ m4/ax_check_link_flag.m4			\
	m4/ax_check_preproc_flag.m4
dist_noinst_DATA = LICENSE.QPL

PKG_VER := $(shell ${top_srcdir}/scripts/vcversion -r $(top_srcdir) -n)

# ---- Version ----
#
nodist_noinst_DATA = VERSION.psmgmt

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
if BUILD_WITHOUT_PSCONFIG
  AM_CONFIGARGS := $(AM_CONFIGARGS) -without psconfig
endif
if !MUNGE_ENABLED
  AM_CONFIGARGS := $(AM_CONFIGARGS) -without munge
endif
if !PAM_ENABLED
  AM_CONFIGARGS := $(AM_CONFIGARGS) -without pam
endif
if !SPANK_ENABLED
  AM_CONFIGARGS := $(AM_CONFIGARGS) -without spank
endif
if !HAVE_SYSTEMD
  AM_CONFIGARGS := $(AM_CONFIGARGS) -without systemd
endif
if BUILD_WITHOUT_TESTPLUGINS
  AM_CONFIGARGS := $(AM_CONFIGARGS) -without testplugins
endif
if !BPF_ENABLED
  AM_CONFIGARGS := $(AM_CONFIGARGS) -without bpf
endif
if BUILD_WITH_PSMOM
  AM_CONFIGARGS := $(AM_CONFIGARGS) -with psmom
endif

.PHONY: tar rpm srpm tag version
tar rpm srpm tag version:
	$(MAKE) TOP_SRCDIR=$(abs_top_srcdir) CONFIGARGS="$(AM_CONFIGARGS) $(CONFIGARGS)" -C dist $@

# keep "make distcheck" happy
AM_DISTCHECK_CONFIGURE_FLAGS =                         \
       --with-unitdir=$$dc_install_base/$(unitdir)     \
       --with-pamdir=$$dc_install_base/$(pamdir)
