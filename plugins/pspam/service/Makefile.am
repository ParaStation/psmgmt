#
# ParaStation
#
# Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2024-2025 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon		\
	       -I$(top_srcdir)/plugins/pspam/include
AM_CFLAGS += -fcommon

plugin_LTLIBRARIES = pam_service.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

pam_service_la_SOURCES = pamservice.c pamservice_handles.h	\
	pamservice_kvs.c pamservice_log.c pamservice_log.h	\
	pamservice_types.h

pam_service_la_LIBADD = -lpam $(libplugincommon) $(libpscommon)
