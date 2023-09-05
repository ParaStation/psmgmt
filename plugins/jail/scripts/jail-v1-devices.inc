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
# CHILD and configuration like CGROUP_BASE, CG_USER, CG_JOB,
# CG_STEP, BASE or SCRIPT set.

function initDevices()
{
    [[ -n $USER ]] || elog "no user env variable for job $JOBID"
    mdsave "$CG_USER"

    [[ -n $JOBID ]] || elog "no jobid for pid $CHILD user $USER"
    mdsave "$CG_JOB"

    [[ -n $STEPID ]] && mdsave "$CG_STEP"

    dlog "adding child $CHILD to $CG_JOB/tasks"
    cgWrite "$CHILD" "$CG_JOB/tasks"
    # step cgroup
    [[ -n $STEPID ]] && {
	dlog "adding child $CHILD to $CG_STEP/tasks"
	cgWrite "$CHILD" "$CG_STEP/tasks"
    }
}

function setDev()
{
    local ACTION=$1
    local DEV=$2
    local RANGE=$3

    if [[ $RANGE == "USER" ]]; then
	dlog "$DEV > $CG_USER/$ACTION"
	cgWrite "$DEV" "$CG_USER/$ACTION"
	return
    fi

    dlog "$DEV > $CG_JOB/$ACTION"
    cgWrite "$DEV" "$CG_JOB/$ACTION"

    # step cgroup
    [[ -n $STEPID ]] && {
	dlog "$DEV > $CG_STEP/$ACTION"
	cgWrite "$DEV" "$CG_STEP/$ACTION"
    }
}

function setDevices()
{
    local TYPE=$1
    local RANGE=$2

    # set denied devices
    if [[ $TYPE == "ALL" || $TYPE == "DENY" ]]; then
	cnt=0
	while true; do
	    next="__PSJAIL_DEV_DENY_$cnt"
	    [[ -z ${!next} ]] && break
	    dlog "deny device$cnt = ${!next}"

	    setDev "devices.deny" "${!next}" "$RANGE"

	    cnt=$(( cnt + 1 ))
	done
    fi

    # set allowed devices
    if [[ $TYPE == "ALL" || $TYPE == "ALLOW" ]]; then
	cnt=0
	while true; do
	    next="__PSJAIL_DEV_ALLOW_$cnt"
	    [[ -z ${!next} ]] && break
	    dlog "allow device$cnt = ${!next}"

	    setDev "devices.allow" "${!next}" "$RANGE"

	    cnt=$(( cnt + 1 ))
	done
    fi
}

function constrainSingleUser()
{
    # ssh login via pspam/pspmix server needs an existing user cgroup
    if [[ -d $CG_USER ]]; then
	# add user to cgroup
	dlog "adding child $CHILD to user cgroup $CG_USER"
	cgWrite "$CHILD" "$CG_USER/tasks"
    else
	elog "error: user cgroup $CG_USER is missing"
    fi
}

function initUserDevices()
{
    [[ -n $USER ]] || elog "no user env variable for job $JOBID"

    if [[ ! -d $CG_USER ]]; then
	mdsave "$CG_USER"

	# deny all devices only when setup new user cgroup
	setDevices "DENY" "USER"
    fi

    # set allowed devices (will overrule previous denied devices)
    setDevices "ALLOW" "USER"
}

if [[ $CONSTRAIN_DEVICES == "no" ]]; then
    dlog "constrain devices *disabled*"
else
    dlog "constrain devices enabled"

    getLock

    if [[ -n $ADD_USER_TO_CGROUP ]]; then
	constrainSingleUser
    elif [[ -n $USER_INIT ]]; then
	initUserDevices
    else
	initDevices
	setDevices "ALL" "JOB"
    fi

    rmLock
fi

# vim: set filetype=sh ts=8 sw=4 tw=0 sts=4 noet :
