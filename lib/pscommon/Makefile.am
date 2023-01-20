#
# ParaStation
#
# Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
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

noinst_LTLIBRARIES = libpscommon.la

libpscommon_la_SOURCES = logging.c psitems.c pscommon.c pscpu.c	\
	psenv.c pscomplist.c pspartition.c psprotocol.c		\
	psreservation.c psserial.c pssignal.c pstask.c pscio.c

psserial.lo: CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/bin/daemon

libpscommon_la_LIBADD = -ldl
