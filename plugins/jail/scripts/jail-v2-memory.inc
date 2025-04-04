#!/bin/bash
#
# ParaStation
#
# Copyright (C) 2022-2025 ParTec AG, Munich
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
# CHILD and configuration like CGROUP_BASE, CG_USER, CG_JOB,
# CG_STEP, BASE or SCRIPT set.

IS_INIT="false"

function initMem()
{
    [[ -n $JOBID ]] || elog "no jobid for pid $CHILD user $USER"

    dlog "adding child $CHILD to $CG_JOB/tasks/cgroup.procs"
    cgWrite "$CHILD" "$CG_JOB/tasks/cgroup.procs"

    # step cgroup
    [[ -n $STEPID ]] && {
	dlog "adding child $CHILD to $CG_STEP/tasks/cgroup.procs"
	cgWrite "$CHILD" "$CG_STEP/tasks/cgroup.procs"
    }
    IS_INIT="true"
}

function setUserMem()
{
    [[ $MODIFY_CGROUPS == 0 ]] && return

    local LIMIT_VAL=$1
    local LIMIT_NAME=$2

    if [[ -z $LIMIT_NAME || -z $LIMIT_VAL || $LIMIT_VAL -lt 1 ]]; then
	dlog "Skipping invalid limit $LIMIT_VAL for $LIMIT_NAME"
    else
	# user cgroup
	PRE_LIMIT=$(cat "$CG_USER/$LIMIT_NAME")
	[[ -n $PRE_LIMIT && $PRE_LIMIT != "max" ]] && {
	    LIMIT_VAL=$((LIMIT_VAL + PRE_LIMIT))
	}

	dlog "$LIMIT_VAL > $CG_USER/$LIMIT_NAME User: $USER"
	cgWrite "$LIMIT_VAL" "$CG_USER/$LIMIT_NAME"
    fi
}

function setJobMem()
{
    [[ $MODIFY_CGROUPS == 0 ]] && return

    local LIMIT_VAL=$1
    local LIMIT_NAME=$2

    [[ $IS_INIT == "false" ]] && initMem;

    if [[ -z $LIMIT_NAME || -z $LIMIT_VAL || $LIMIT_VAL -lt 1 ]]; then
	dlog "Skipping invalid limit $LIMIT_VAL for $LIMIT_NAME"
    else
	# job cgroup
	dlog "$LIMIT_VAL > $CG_JOB/$LIMIT_NAME Job: $JOBID:$STEPID"
	cgWrite "$LIMIT_VAL" "$CG_JOB/$LIMIT_NAME"
    fi
}

function setStepMem()
{
    [[ $MODIFY_CGROUPS == 0 ]] && return

    local LIMIT_VAL=$1
    local LIMIT_NAME=$2

    [[ $IS_INIT == "false" ]] && initMem;

    # step cgroup
    [[ -n $STEPID ]] && {
	if [[ -z $LIMIT_NAME || -z $LIMIT_VAL || $LIMIT_VAL -lt 1 ]]; then
	    dlog "Skipping invalid limit $LIMIT_VAL for $LIMIT_NAME"
	else
	    dlog "$LIMIT_VAL > $CG_STEP/$LIMIT_NAME"
	    cgWrite "$LIMIT_VAL" "$CG_STEP/$LIMIT_NAME"
	fi
    }
}

function constrainRam()
{
    setJobMem "$JOB_RAM_SOFT" "memory.high"
    setJobMem "$JOB_RAM_HARD" "memory.max"

    setStepMem "$STEP_RAM_SOFT" "memory.high"
    setStepMem "$STEP_RAM_HARD" "memory.max"
}

function constrainUser()
{
    local TYPE=$1

    if [[ $TYPE == "RAM" ]]; then
	setUserMem "$JOB_RAM_SOFT" "memory.high"
	setUserMem "$JOB_RAM_HARD" "memory.max"
    elif [[ $TYPE == "SWAP" ]]; then
	setUserMem "$JOB_SWAP" "memory.swap.max"
    else
	elog "invalid memory type"
    fi
}

function constrainMemSingleUser()
{
    # ssh login via pspam/pspmix server needs an existing user cgroup
    if [[ -d $CG_USER ]]; then
	    # add user to cgroup
	    dlog "adding child $CHILD to user cgroup $CG_USER"
	    cgWrite "$CHILD" "$CG_USER/tasks/cgroup.procs"
    else
	    elog "error: user cgroup $CG_USER is missing"
    fi
}

if [[ $CONSTRAIN_RAM == "no" ]]; then
    dlog "constrain RAM *disabled*"
else
    dlog "constrain RAM enabled"

    if [[ -n $ADD_USER_TO_CGROUP ]]; then
	constrainMemSingleUser
    elif [[ -n $USER_INIT ]]; then
	constrainUser "RAM"
    else
	constrainRam
    fi
fi

if [[ $CONSTRAIN_SWAP == "no" ]]; then
    dlog "constrain SWAP *disabled*"
else
    dlog "constrain SWAP enabled"

    if [[ -n $ADD_USER_TO_CGROUP ]]; then
	constrainMemSingleUser
    elif [[ -n $USER_INIT ]]; then
	constrainUser "SWAP"
    else
	setJobMem "$JOB_SWAP" "memory.swap.max"
	setStepMem "$STEP_SWAP" "memory.swap.max"
    fi
fi

# vim: set filetype=sh ts=8 sw=4 tw=0 sts=4 noet :
