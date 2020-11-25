#
# ParaStation
#
# Copyright (C) 2012-2020 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon
AM_CFLAGS += $(glib2_CFLAGS)
AM_LDFLAGS += $(glib2_LDFLAGS)

noinst_LTLIBRARIES = libputil.la

libputil_la_SOURCES = config_parsing.c env.c hardware.c mcast.c	\
	parser.c psdaemonprotocol.c rdp.c selector.c timer.c	\
	psparamspace.c old_config_parsing.c linenoise.c

libputil_la_LIBADD = -lm
