#
# ParaStation
#
# Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2023 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

pkglibexec_PROGRAMS = psilogger

libputil = $(top_builddir)/lib/putil/libputil.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la
libpslog = $(top_builddir)/lib/pslog/libpslog.la

psilogger_SOURCES = psilogger.c psilogger.h psiloggermerge.c	\
	psiloggermerge.h psiloggerclient.c psiloggerclient.h
psilogger_LDADD = $(libputil) $(libpslog) $(libpscommon) -lrt
