#
# ParaStation
#
# Copyright (C) 2012-2020 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022-2024 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

SUBDIRS = dynIP jail nodeinfo pelogue psaccount psexec psgw pspam	\
	pspmi psslurm rrcomm

if MUNGE_ENABLED
SUBDIRS += psmunge
endif

if !BUILD_WITHOUT_PSPMIX
SUBDIRS += pspmix
endif
