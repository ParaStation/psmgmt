#
# ParaStation
#
# Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

bin_PROGRAMS = psiadmin

noinst_HEADERS = helpmsgs.c

libpse = $(top_builddir)/lib/pse/libpse.la
libputil = $(top_builddir)/lib/putil/libputil.la

psiadmin_SOURCES = psiadmin.c psiadmin.h adminparser.c adminparser.h	\
	commands.c commands.h
psiadmin_LDADD = $(libpse) $(libputil) $(POPT_LIBS)
