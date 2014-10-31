#
# ParaStation
#
# Copyright (C) 2012-2014 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Thomas Moschny <moschny@par-tec.com>
#

include $(top_srcdir)/common.am

AM_CPPFLAGS += -DPREFIX=\"$(prefix)\"

noinst_LTLIBRARIES = libpscommon.la

libpscommon_la_SOURCES = logging.c pscommon.c pscpu.c pspartition.c	\
	psprotocol.c pstask.c
