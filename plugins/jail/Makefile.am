#
# ParaStation
#
# Copyright (C) 2018-2020 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

plugin_LTLIBRARIES = jail.la

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

jail_la_SOURCES = jail.c jailconfig.c jailconfig.h jaillog.c jaillog.h

jail_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGINDIR=\"$(plugindir)\"
jail_la_LIBADD = $(libplugincommon) $(libpscommon)

dist_plugin_DATA = jail.conf
dist_plugin_SCRIPTS = jail-config.inc jail-cpuacct.inc jail-cpuset.inc \
		      jail-functions.inc jail-main.sh jail-oom.inc \
		      jail-term-cpuacct.inc jail-term-cpuset.inc
