#
# ParaStation
#
# Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2021-2022 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version -module
AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

libplugincommon = $(top_builddir)/lib/plugincommon/libplugincommon.la
libpscommon = $(top_builddir)/lib/pscommon/libpscommon.la

noinst_LTLIBRARIES = plugin1.la plugin2.la plugin3.la plugin4.la	\
	plugin5.la plugin6.la plugin7.la plugin8.la plugin9.la		\
	dyn_resources.la fixLoginuid.la pluginpsconfig.la oldHook.la
if SPANK_ENABLED
noinst_LTLIBRARIES += spank_test.la
endif

plugin_LTLIBRARIES = no_flowcontrol.la delaySlurmMsg.la			\
	delegateStat.la psBlackHole.la fakenode.la delayPSPMsg.la

delayPSPMsg_la_LIBADD = $(libplugincommon) $(libpscommon)

delaySlurmMsg_la_CPPFLAGS = $(AM_CPPFLAGS)		\
	-I$(top_srcdir)/plugins/psslurm/src/psslurm	\
	-I$(top_srcdir)/plugins/psslurm/src/common

delegateStat_la_LIBADD = $(libplugincommon) $(libpscommon)

fakenode_la_CPPFLAGS = $(AM_CPPFLAGS)		\
	-I$(top_srcdir)/plugins/nodeinfo
fakenode_la_LIBADD = $(libplugincommon) $(libpscommon)

oldHook_la_LIBADD = $(libplugincommon) $(libpscommon)

pluginpsconfig_la_LIBADD = $(libplugincommon) $(libpscommon)

psBlackHole_la_LIBADD = $(libplugincommon) $(libpscommon)

if SPANK_ENABLED
spank_test_la_CPPFLAGS = $(AM_CPPFLAGS) -DSLURMLIBDIR=\"$(slurmlibdir)\"
endif
