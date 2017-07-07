#
# ParaStation
#
# Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

PKG_VER := $(shell ${top_srcdir}/scripts/vcversion -r $(top_srcdir) --fmt version -n)
PKG_REL := $(shell ${top_srcdir}/scripts/vcversion -r $(top_srcdir) --fmt release -n)

AM_CPPFLAGS += -DVERSION_psmgmt=\"$(PKG_VER)\"
AM_CPPFLAGS += -DRELEASE_psmgmt=\"$(PKG_REL)\"

bin_PROGRAMS = psiadmin

noinst_HEADERS = helpmsgs.c

libpse = $(top_builddir)/lib/pse/libpse.la
libputil = $(top_builddir)/lib/putil/libputil.la

psiadmin_SOURCES = psiadmin.c psiadmin.h adminparser.c adminparser.h	\
	commands.c commands.h
psiadmin_LDADD = $(libpse) $(libputil) $(POPT_LIBS)
