#!/bin/bash
#
# ParaStation
#
# Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2021-2023 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
#
# Script to be executed by ParaStation's jail plugin each time a
# process will be jailed into the cgroup. Nevertheless, this
# functionality is independent of the actual cgroup plugin.
#
# This script is executed with the same permissions as the ParaStation
# daemon psid, i.e. typically with root permissions! Thus, special care
# has to be taken when changing this script.
#
# This script will be called by the jail plugin via system() and get
# the process ID of the process to be jailed as an argument.

SELF=$(realpath $0)
CommandName=${0##*/}
CommandPath=${SELF%/*}
CHILD=${1}
SCRIPT=${CommandName%%.*}

source $CommandPath/jail-functions.inc
source $CommandPath/jail-config.inc

initJailEnv

exec 2>>$LOG_FILE 1>&2

if [ -z "$CHILD" ] || [ "$CHILD" == "0" ]; then
    dlog "Skipping invalid child $CHILD"
    exit 0
fi

if [ "$CGROUP_VERSION" == "v2" ]; then
    BASE="$CGROUP_BASE/$PREFIX-$PSID_PID"
    CG_USER="$BASE/$USER"
    CG_JOB="$CG_USER/job-$JOBID"
    CG_STEP="$CG_JOB/step-$JOBID:$STEPID"
fi

for modName in ${MODULES//,/$IFS}; do
    MODULE="$CommandPath/$SCRIPT-$CGROUP_VERSION-$modName.inc"
    [ -r $MODULE ] || {
	[ $SCRIPT != "jail-term" ] && {
	    mlog "$SCRIPT module $MODULE not found"
	}
	continue
    }

    if [ "$CGROUP_VERSION" == "v1" ]; then
	BASE="$CGROUP_BASE/$modName"
	CG_USER="$BASE/$PREFIX-$PSID_PID-$USER"
	CG_JOB="$CG_USER/job-$JOBID"
	CG_STEP="$CG_JOB/step-$STEPID"
    fi

    dlog "Calling module $MODULE for child $CHILD"
    source $MODULE
done

exit 0
