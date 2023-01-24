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


function initDevices()
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
    echo "$CHILD" > $CG_JOB/tasks
    # step cgroup
    [ -n "$STEPID" ] && {
	dlog "adding child $CHILD to $CG_STEP/tasks"
	echo "$CHILD" > $CG_STEP/tasks
    }
}

function setDev()
{
    local ACTION=$1
    local DEV=$2

    dlog "$DEV > $CG_JOB/$ACTION"
    echo $DEV > $CG_JOB/$ACTION

    # step cgroup
    [ -n "$STEPID" ] && {
	dlog "$DEV > $CG_STEP/$ACTION"
	echo $DEV > $CG_STEP/$ACTION
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

	setDev "devices.allow" "${!next}"

	cnt=$(( cnt + 1 ))
    done

    # set denied devices
    cnt=0
    while true; do
	next="__PSJAIL_DEV_DENY_$cnt"
	[ -z "${!next}" ] && break
	dlog "deny device$cnt = ${!next}"

	setDev "devices.deny" "${!next}"

	cnt=$(( cnt + 1 ))
    done
}

function constrainDevSSH()
{
    # ssh login via pspam which needs an existing user cgroup
    CG_USER="$BASE/$PREFIX-$PSPAM_USER"
    if [ -d $CG_USER ]; then
	    # add user to cgroup
	    mlog "adding child $CHILD to user cgroup $CG_USER"
	    echo "$CHILD" > $CG_USER/tasks
    else
	    elog "error: user cgroup $CG_USER is missing"
    fi
}

if [ "$CONSTRAIN_DEVICES" == "no" ]; then
    dlog "constrain devices *disabled*"
else
    dlog "constrain devices enabled"

    if [ -n "$PSPAM_USER" ]; then
	constrainDevSSH
    else
	initDevices
	setDevices
    fi
fi
