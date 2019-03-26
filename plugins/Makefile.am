#
# ParaStation
#
# Copyright (C) 2012-2018 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

SUBDIRS = cgroup jail pelogue psaccount psexec psmom pspam pspmi	\
	psresport psslurm

if MUNGE_ENABLED
SUBDIRS += psmunge
endif

if !BUILD_WITHOUT_PSPMIX
SUBDIRS += pspmix
endif

