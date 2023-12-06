#
# ParaStation
#
# Copyright (C) 2022-2023 ParTec AG, Munich
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

AM_CPPFLAGS += -DPREFIX=\"$(prefix)\"

AM_CPPFLAGS += -I$(srcdir)/../include
AM_CFLAGS += -fPIC
AM_LDFLAGS += -avoid-version

lib_LIBRARIES = librrcomm.a

librrcomm_a_SOURCES = rrcomm.c $(top_srcdir)/lib/pscommon/pscio.c	\
	$(top_srcdir)/lib/pscommon/pscommon.c				\
	$(top_srcdir)/lib/pscommon/logging.c
