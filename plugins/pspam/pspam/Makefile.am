#
# ParaStation
#
# Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2024 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon		\
		-I$(top_srcdir)/plugins/psaccount	\
		-I$(top_srcdir)/plugins/pspam/include
AM_CFLAGS += -fcommon

plugin_LTLIBRARIES = pspam.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

pspam_la_SOURCES = pspam.c pspamcomm.c pspamcomm.h pspamhandles.h	\
	pspaminter.c pspaminter.h pspamkvs.c pspamlog.c pspamlog.h	\
	pspamssh.c pspamssh.h pspamtypes.h pspamuser.c pspamuser.h

pspam_la_LIBADD = $(libplugincommon) $(libpscommon)
