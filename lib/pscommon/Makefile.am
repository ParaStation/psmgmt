#
# ParaStation
#
# Copyright (C) 2012-2018 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

AM_CPPFLAGS += -DPREFIX=\"$(prefix)\"

noinst_LTLIBRARIES = libpscommon.la

libpscommon_la_SOURCES = logging.c pscommon.c pscpu.c psenv.c	\
	pshostlist.c pspartition.c psprotocol.c psreservation.c	\
	psserial.c pssignal.c pstask.c

psserial.lo: CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/bin/daemon

libpscommon_la_LIBADD = -ldl
