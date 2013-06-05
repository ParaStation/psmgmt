#
# ParaStation
#
# Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Thomas Moschny <moschny@par-tec.com>
#

include $(top_srcdir)/common.am

AM_CPPFLAGS += -I$(top_srcdir)/bin/daemon

noinst_LTLIBRARIES = libputil.la

libputil_la_SOURCES = config_parsing.c env.c hardware.c mcast.c \
	parser.c psdaemonprotocol.c psstrings.c rdp.c selector.c \
	timer.c psparamspace.c
libputil_la_LIBADD = -lm
