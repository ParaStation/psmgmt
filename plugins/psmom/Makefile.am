#
# ParaStation
#
# Copyright (C) 2013-2014 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Michael Rauh <rauh@par-tec.com>
#

include $(top_srcdir)/common.am

SUBDIRS = doc src/include src/pbs src/psmom

if PAM_ENABLED
SUBDIRS += src/pam
endif

dist_noinst_DATA = LICENSE.OpenPBS README ChangeLog-History
