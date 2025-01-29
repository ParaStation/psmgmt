#
# ParaStation
#
# Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2025 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

SUBDIRS = doc

if SPANK_ENABLED
SUBDIRS += spank route
endif

pelogue_dir   := plugins/pelogue
psexec_dir    := plugins/psexec
psaccount_dir := plugins/psaccount
psslurm_dir   := plugins/psslurm/src/psslurm
slurm_dir     := plugins/psslurm/src/common

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon \
	       -I$(top_srcdir)/$(pelogue_dir) \
	       -I$(top_srcdir)/$(psslurm_dir) \
	       -I$(top_srcdir)/$(slurm_dir) \
	       -I$(top_srcdir)/$(psexec_dir) \
	       -I$(top_srcdir)/$(psaccount_dir)
AM_CFLAGS += -fcommon

plugin_LTLIBRARIES = psgw.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

psgw_la_SOURCES = psgw.c psgwlog.c psgwlog.h psgwres.c psgwres.h \
		  psgwconfig.c psgwconfig.h psgwkvs.c psgwrequest.c \
		  psgwrequest.h psgwpart.c psgwpart.h psgwcomm.h psgwcomm.c

psgw_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\"		\
		-DPKGSYSCONFDIR=\"$(sysconfdir)/parastation\"		\
		-DBINDIR=\"$(bindir)\"	-DPSGWLIBDIR=\"$(psgwlibdir)\"

psgw_la_LIBADD = $(libplugincommon) $(libpscommon)
