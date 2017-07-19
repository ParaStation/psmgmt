#
# ParaStation
#
# Copyright (C) 2013-2017 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

libputil = $(top_builddir)/lib/putil/libputil.la
libpslog = $(top_builddir)/lib/pslog/libpslog.la

noinst_LTLIBRARIES = libpskvs.la

libpskvs_la_SOURCES = kvs.c kvscommon.c kvslog.c kvsprovider.c
libpskvs_la_LIBADD = $(libputil) $(libpslog)
