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

function loadBPF()
{
    local BPF_PATH=$1
    local BPF_ID=$2

    local QUIET=""
    [ "$DEBUG" == "0" ] && QUIET="-q"
    
    "$BPF_DEVICE_LOADER" -s -i "$BPF_ID" "$QUIET" 1>/dev/null 2>&1 && {
	dlog "skip loading for $BPF_PATH with ID $BPF_ID"
	return
    }

    "$BPF_DEVICE_LOADER" -l "$BPF_DEVICE_PROG" -p "$BPF_PATH" -i "$BPF_ID" || {
	elog "attaching $BPF_DEVICE_PROG to $BPF_PATH with ID $BPF_ID failed"
    }

    dlog "attached $BPF_DEVICE_PROG to $BPF_PATH with ID $BPF_ID"
}

function initDevices()
{
    [ -n "$USER" ] || elog "no user env variable for job $JOBID"

    getLock

    mdsave "$CG_USER"
    loadBPF "$CG_USER/tasks" USER_"$USER"

    mdsave "$CG_JOB"
    loadBPF "$CG_JOB/tasks" JOB_"$JOBID"

    [ -n "$STEPID" ] && {
	mdsave "$CG_STEP"
	loadBPF "$CG_STEP/tasks" STEP_"$JOBID"_"$STEPID"
    }

    rmLock

    dlog "adding child $CHILD to $CG_JOB/tasks/cgroup.procs"
    cgWrite "$CHILD" "$CG_JOB/tasks/cgroup.procs"
    # step cgroup
    [ -n "$STEPID" ] && {
	dlog "adding child $CHILD to $CG_STEP/tasks/cgroup.procs"
	cgWrite "$CHILD" "$CG_STEP/tasks/cgroup.procs"
    }
}

function setDev()
{
    local ACTION=$1
    local DEV=${2#* }
    DEV=${DEV% *}

    local QUIET=""
    [ "$DEBUG" == "0" ] && QUIET="-q"

    dlog "Set $ACTION for $DEV $JOBID"
    "$BPF_DEVICE_LOADER" -i JOB_"$JOBID" "$ACTION" "$DEV" "$QUIET" || {
	elog "set JOB access for device $DEV failed"
    }

    # step cgroup
    [ -n "$STEPID" ] && {
	dlog "Set $ACTION for $DEV $STEPID"
	"$BPF_DEVICE_LOADER" -i STEP_"$JOBID"_"$STEPID" "$ACTION" "$DEV" "$QUIET" || {
	    elog "set STEP access for device $DEV failed"
	}
    }
}

function setDevices()
{
    # set allowed devices
    cnt=0
    while true; do
	next="__PSJAIL_DEV_ALLOW_$cnt"
	[ -z "${!next}" ] && break
	dlog "allow device$cnt = ${!next}"

	setDev "-a" "${!next}"

	cnt=$(( cnt + 1 ))
    done

    # set denied devices
    cnt=0
    while true; do
	next="__PSJAIL_DEV_DENY_$cnt"
	[ -z "${!next}" ] && break
	dlog "deny device$cnt = ${!next}"

	setDev "-d" "${!next}"

	cnt=$(( cnt + 1 ))
    done
}

function constrainSingleUser()
{
    # ssh login via pspam/pspmix server needs an existing user cgroup
    if [ -d "$CG_USER" ]; then
	# add user to cgroup
	dlog "adding child $CHILD to user cgroup $CG_USER"
	cgWrite "$CHILD" "$CG_USER/tasks/cgroup.procs"
    else
	elog "error: user cgroup $CG_USER is missing"
    fi
}

if [ "$CONSTRAIN_DEVICES" == "no" ]; then
    dlog "constrain devices *disabled*"
else
    dlog "constrain devices enabled"

    if [ -n "$ADD_USER_TO_CGROUP" ]; then
	constrainSingleUser
    else
	initDevices
	setDevices
    fi
fi
