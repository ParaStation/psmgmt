#
# ParaStation
#
# Copyright (C) 2013-2020 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

psaccount_dir := plugins/psaccount

AM_CFLAGS += -std=gnu99
AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon -I$(top_srcdir)/$(psaccount_dir)
AM_CFLAGS += -fcommon

plugin_LTLIBRARIES = pelogue.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

pelogue_la_SOURCES = pelogue.c peloguelog.c peloguelog.h		\
	peloguecomm.c peloguecomm.h pelogueconfig.c pelogueconfig.h	\
	peloguechild.c peloguechild.h peloguescript.c peloguescript.h	\
	peloguejob.c peloguejob.h pelogueinter.c pelogueinter.h		\
	pelogueforwarder.c pelogueforwarder.h peloguekvs.c		\
	peloguehandles.h peloguetypes.h

pelogue_la_LIBADD = $(libplugincommon) $(libpscommon)

pelogue_la_CPPFLAGS = $(AM_CPPFLAGS) -DLOCALSTATEDIR=\"$(localstatedir)\"

install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/scripts
