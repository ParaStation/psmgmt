#!/bin/bash
#
# ParaStation
#
# Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2021-2025 ParTec AG, Munich
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

MODULES=${MODULES-undef}

CommandName=$(basename "$0")
CommandPath=$(dirname "$0")
CHILD=${1}
SCRIPT=${CommandName%%.*}
modName="main"

# shellcheck source=/dev/null
source "$CommandPath"/jail-functions.inc
# shellcheck source=/dev/null
source "$CommandPath"/jail-config.inc

initJailEnv

exec 2>>"$LOG_FILE" 1>&2

if [[ -z $CHILD || $CHILD == 0 ]]; then
    dlog "Skipping invalid child $CHILD"
    exit 0
fi

BASE="$CGROUP_BASE/$PREFIX"
CG_USER="$BASE/user-$USER"
CG_JOB="$CG_USER/job-$JOBID"
CG_STEP="$CG_JOB/step-$STEPID"

# jail scripts might be called for every forwarder and child process. Therefore,
# MODIFY_CGROUPS will only be set to 1 if there is the actual necessity to
# modify the cgroup tree (e.g. create sub-directories, enable controllers, or
# change limits). If MODIFY_CGROUPS is set to 0, jail scripts will only add
# additional PIDs to the cgroup tree. Since the limits for all processes locked
# inside a cgroup are the same, it is sufficient that the first jailed process
# will trigger the setup of the cgroup.
#
# In contrast jail-term scripts will only be called once for every step, job or
# allocation. Hence MODIFY_CGROUPS will always be set to 1 for them.
if [[ $SCRIPT == "jail-term" || ! -d $CG_USER || -n $JOBID && ! -d $CG_JOB
      || -n $STEPID && ! -d $CG_STEP ]]; then
    MODIFY_CGROUPS=1
    getExclusiveUserLock
else
    MODIFY_CGROUPS=0
    getSharedUserLock
fi

[[ -n $USER ]] || elog "user env variable not set"
[[ -d $CG_USER ]] || enableControllers "$CG_USER"
[[ -n $JOBID && ! -d $CG_JOB ]] && enableControllers "$CG_JOB"
[[ -n $STEPID && ! -d $CG_STEP ]] && enableControllers "$CG_STEP"

for modName in ${MODULES//,/$IFS}; do
    MODULE="$CommandPath/$SCRIPT-$CGROUP_VERSION-$modName.inc"
    [[ -r $MODULE ]] || {
	[[ $SCRIPT != "jail-term" ]] && {
	    mlog "$SCRIPT module $MODULE not found"
	}
	continue
    }

    dlog "Calling module $MODULE for child $CHILD modify $MODIFY_CGROUPS"
    # shellcheck source=/dev/null
    source "$MODULE"
done

# cleanup cgroups even if no constraints are enabled via Slurm
if [[ $SCRIPT == "jail-term" ]]; then
    [[ -n $STEPID  && -d $CG_STEP ]] && killTasks "$CG_STEP"
    [[ -d $CG_JOB && -z $STEPID && -n $JOBID ]] && killJob "$CG_JOB"
    [[ -d $CG_USER && -n $ALLOC_LIST
	 && $ALLOC_LIST == "$JOBID" ]] && killUser "$CG_USER"
fi

rmUserLock

exit 0
