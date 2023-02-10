#!/bin/bash
#
# ParaStation
#
# Copyright (C) 2022-2023 ParTec AG, Munich
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
# This script will be called by the jail plugin via system() and
# jail-main.sh with the process ID of the process to be jailed in
# CHILD and configuration like CGROUP_BASE, BASE or SCRIPT set.

IS_INIT="false"

function initMem()
{
    [ -n "$USER" ] || {
	    elog "no user env variable for job $JOBID"
    }

    CG_USER="$BASE/$PREFIX-$USER"
    mdsave "$CG_USER"

    [ -n "$JOBID" ] || elog "no jobid for pid $CHILD user $USER"

    CG_JOB="$CG_USER/job-$JOBID"
    mdsave "$CG_JOB"

    [ -n "$STEPID" ] && {
	    CG_STEP="$CG_JOB/step-$STEPID"
	    mdsave "$CG_STEP"
    }

    dlog "adding child $CHILD to $CG_JOB/tasks"
    cgWrite "$CHILD" $CG_JOB/tasks

    # step cgroup
    [ -n "$STEPID" ] && {
	dlog "adding child $CHILD to $CG_STEP/tasks"
	cgWrite "$CHILD" $CG_STEP/tasks
    }
    IS_INIT="true"
}

function setJobMem()
{
    local LIMIT_VAL=$1
    local LIMIT_NAME=$2

    [ "$IS_INIT" == "false" ] && initMem;

    if [ -z "$LIMIT" ] || [ "$LIMIT_VAL" -lt 1 ]; then
	dlog "Skipping invalid limit $LIMIT_VAL for $LIMIT_NAME"
    else
	# job cgroup
	dlog "$LIMIT_VAL > $CG_JOB/$LIMIT_NAME Job: $JOBID:$STEPID"
	cgWrite "$LIMIT_VAL" $CG_JOB/$LIMIT_NAME
    fi

}

function setStepMem()
{
    local LIMIT_VAL=$1
    local LIMIT_NAME=$2

    [ "$IS_INIT" == "false" ] && initMem;

    # step cgroup
    [ -n "$STEPID" ] && {
	if [ -z "$LIMIT" ] || [ "$LIMIT_VAL" -lt 1 ]; then
	    dlog "Skipping invalid limit $LIMIT_VAL for $LIMIT_NAME"
	else
	    dlog "$LIMIT_VAL > $CG_STEP/$LIMIT_NAME"
	    cgWrite "$LIMIT_VAL" $CG_STEP/$LIMIT_NAME
	fi
    }
}

function constrainRam()
{
    setJobMem "$JOB_RAM_SOFT" "memory.soft_limit_in_bytes"
    setJobMem "$JOB_RAM_HARD" "memory.limit_in_bytes"

    setStepMem "$STEP_RAM_SOFT" "memory.soft_limit_in_bytes"
    setStepMem "$STEP_RAM_HARD" "memory.limit_in_bytes"
}

function constrainMemSSH()
{
    # ssh login via pspam which needs an existing user cgroup
    CG_USER="$BASE/$PREFIX-$PSPAM_USER"
    if [ -d $CG_USER ]; then
	    # add user to cgroup
	    dlog "adding child $CHILD to user cgroup $CG_USER"
	    cgWrite "$CHILD" $CG_USER/tasks
    else
	    elog "error: user cgroup $CG_USER is missing"
    fi
}

if [ "$CONSTRAIN_RAM" == "no" ]; then
    dlog "constrain RAM *disabled*"
else
    dlog "constrain RAM enabled"

    if [ -n "$PSPAM_USER" ]; then
	constrainMemSSH
    else
	constrainRam
    fi

    if [ "$CONSTRAIN_KMEM" == "no" ]; then
	dlog "constrain KMEM *disabled*"
    else
	dlog "constrain KMEM enabled"
	if [ -n "$PSPAM_USER" ]; then
	    constrainMemSSH
	else
	    setJobMem "$JOB_KMEM" "memory.kmem.limit_in_bytes"
	    setStepMem "$STEP_KMEM" "memory.kmem.limit_in_bytes"
	fi
    fi
fi

if [ "$CONSTRAIN_SWAP" == "no" ]; then
    dlog "constrain SWAP *disabled*"
else
    dlog "constrain SWAP enabled"
    if [ -n "$PSPAM_USER" ]; then
	constrainMemSSH
    else
	setJobMem "$JOB_SWAP" "memory.memsw.limit_in_bytes"
	setStepMem "$STEP_SWAP" "memory.memsw.limit_in_bytes"
    fi
fi

if [ -z "$SWAPPINESS" ]; then
    dlog "constrain swappiness *disabled*"
else
    dlog "constrain swappiness enabled"

    if [ -n "$PSPAM_USER" ]; then
	constrainMemSSH
    else
	setJobMem "$SWAPPINESS" "memory.swappiness"
	setStepMem "$SWAPPINESS" "memory.swappiness"
    fi
fi
