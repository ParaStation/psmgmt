#
# ParaStation
#
# Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Michael Rauh <rauh@par-tec.com>
#

include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon -I$(srcdir)/../include

plugin_LTLIBRARIES = pelogue.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la

pelogue_la_SOURCES = pelogue.c pelogue.h peloguelog.c peloguelog.h \
	peloguecomm.c peloguecomm.h pelogueconfig.c pelogueconfig.h \
	peloguechild.c peloguechild.h peloguescript.c peloguescript.h \
	peloguejob.c peloguejob.h pelogueinter.c pelogueinter.h \
	pelogueforwarder.c pelogueforwarder.h peloguekvs.c peloguekvs.h

pelogue_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\" \
	-DLOCALSTATEDIR=\"$(localstatedir)\"

pelogue_la_LIBADD = $(libpsi) $(libplugincommon)

install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/spool/parastation/scripts
