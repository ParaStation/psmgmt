#
# ParaStation
#
# Copyright (C) 2012-2020 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

SUBDIRS = cgroup jail nodeinfo pelogue psaccount psexec psgw psmom	\
	pspam pspmi psresport psslurm rrcomm

if MUNGE_ENABLED
SUBDIRS += psmunge
endif

if !BUILD_WITHOUT_PSPMIX
SUBDIRS += pspmix
endif
