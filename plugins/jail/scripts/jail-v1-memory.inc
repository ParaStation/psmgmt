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

IS_INIT="false"

function initMem()
{
    [ -n "$USER" ] || {
	    elog "no user env variable for job $JOBID"
    }

    mdsave "$CG_USER"

    [ -n "$JOBID" ] || elog "no jobid for pid $CHILD user $USER"

    mdsave "$CG_JOB"

    [ -n "$STEPID" ] && {
	    mdsave "$CG_STEP"
    }

    dlog "adding child $CHILD to $CG_JOB/tasks"
    cgWrite "$CHILD" "$CG_JOB/tasks"

    # step cgroup
    [ -n "$STEPID" ] && {
	dlog "adding child $CHILD to $CG_STEP/tasks"
	cgWrite "$CHILD" "$CG_STEP/tasks"
    }
    IS_INIT="true"
}

function setJobMem()
{
    local LIMIT_VAL=$1
    local LIMIT_NAME=$2

    [ "$IS_INIT" == "false" ] && initMem;

    if [ -z "$LIMIT_NAME" ] || [ -z "$LIMIT_VAL" ] || \
       [ "$LIMIT_VAL" -lt 1 ]; then
	dlog "Skipping invalid limit $LIMIT_VAL for $LIMIT_NAME"
    else
	# job cgroup
	dlog "$LIMIT_VAL > $CG_JOB/$LIMIT_NAME Job: $JOBID:$STEPID"
	cgWrite "$LIMIT_VAL" "$CG_JOB/$LIMIT_NAME"
    fi
}

function setStepMem()
{
    local LIMIT_VAL=$1
    local LIMIT_NAME=$2

    [ "$IS_INIT" == "false" ] && initMem;

    # step cgroup
    [ -n "$STEPID" ] && {
	if [ -z "$LIMIT_NAME" ] || [ -z "$LIMIT_VAL" ] || \
	   [ "$LIMIT_VAL" -lt 1 ]; then
	    dlog "Skipping invalid limit $LIMIT_VAL for $LIMIT_NAME"
	else
	    dlog "$LIMIT_VAL > $CG_STEP/$LIMIT_NAME"
	    cgWrite "$LIMIT_VAL" "$CG_STEP/$LIMIT_NAME"
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

function constrainMemSingleUser()
{
    # ssh login via pspam/pspmix server needs an existing user cgroup
    if [ -d "$CG_USER" ]; then
	    # add user to cgroup
	    dlog "adding child $CHILD to user cgroup $CG_USER"
	    cgWrite "$CHILD" "$CG_USER/tasks"
    else
	    elog "error: user cgroup $CG_USER is missing"
    fi
}

function setUserMem()
{
    local LIMIT_VAL=$1
    local LIMIT_NAME=$2

    if [ -z "$LIMIT_NAME" ] || [ -z "$LIMIT_VAL" ] || \
       [ "$LIMIT_VAL" -lt 1 ]; then
	dlog "Skipping invalid limit $LIMIT_VAL for $LIMIT_NAME"
    else
	# user cgroup

	if [ -n "$ADD_TO_CGROUP" ] && [ "$ADD_TO_CGROUP" == "yes" ]; then
	    PRE_LIMIT=$(cat "$CG_USER/$LIMIT_NAME")
	    [ -n "$PRE_LIMIT" ] && [ "$PRE_LIMIT" -gt 1 ] && {
		NEW_VAL=$((LIMIT_VAL + PRE_LIMIT))
		[ $NEW_VAL -gt 1 ] && LIMIT_VAL=$NEW_VAL
	    }
	fi

	dlog "$LIMIT_VAL > $CG_USER/$LIMIT_NAME User: $USER"
	cgWrite "$LIMIT_VAL" "$CG_USER/$LIMIT_NAME"
    fi
}

function constrainUser()
{
    local TYPE=$1

    [ -n "$USER" ] || elog "no user env variable for job $JOBID"
    mdsave "$CG_USER"

    if [ "$TYPE" == "RAM" ]; then
	# ensure to set swap limit before hard mem limit if smaller
	SWAP_LIMIT=$(cat "$CG_USER/memory.memsw.limit_in_bytes" 2>/dev/null)
	if [ -n "$SWAP_LIMIT" ] && [ "$SWAP_LIMIT" -lt "$JOB_RAM_HARD" ]; then
	    setUserMem "$JOB_SWAP" "memory.memsw.limit_in_bytes"
	    SET_MEM_SWAP_LIMIT="yes"
	fi

	setUserMem "$JOB_RAM_SOFT" "memory.soft_limit_in_bytes"
	setUserMem "$JOB_RAM_HARD" "memory.limit_in_bytes"
    elif [ "$TYPE" == "KMEM" ]; then
	setUserMem "$JOB_KMEM" "memory.kmem.limit_in_bytes"
    elif [ "$TYPE" == "SWAP" ]; then
	[ "$SET_MEM_SWAP_LIMIT" != "yes" ] && {
	    setUserMem "$JOB_SWAP" "memory.memsw.limit_in_bytes"
	}
    elif [ "$TYPE" == "SWAPPINESS" ]; then
	setUserMem "$SWAPPINESS" "memory.swappiness"
    else
	elog "invalid memory type"
    fi
}

if [ -d "$CG_USER" ]; then
    ADD_TO_CGROUP="yes"
fi

if [ "$CONSTRAIN_RAM" == "no" ]; then
    dlog "constrain RAM *disabled*"
else
    dlog "constrain RAM enabled"

    if [ -n "$ADD_USER_TO_CGROUP" ]; then
	constrainMemSingleUser
    elif [ -n "$USER_INIT" ]; then
	constrainUser "RAM"
    else
	constrainRam
    fi

    if [ "$CONSTRAIN_KMEM" == "no" ]; then
	dlog "constrain KMEM *disabled*"
    else
	dlog "constrain KMEM enabled"
	if [ -n "$ADD_USER_TO_CGROUP" ]; then
	    constrainMemSingleUser
	elif [ -n "$USER_INIT" ]; then
	    constrainUser "KMEM"
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
    if [ -n "$ADD_USER_TO_CGROUP" ]; then
	constrainMemSingleUser
    elif [ -n "$USER_INIT" ]; then
	constrainUser "SWAP"
    else
	setJobMem "$JOB_SWAP" "memory.memsw.limit_in_bytes"
	setStepMem "$STEP_SWAP" "memory.memsw.limit_in_bytes"
    fi
fi

if [ -z "$SWAPPINESS" ]; then
    dlog "constrain swappiness *disabled*"
else
    dlog "constrain swappiness enabled"

    if [ -n "$ADD_USER_TO_CGROUP" ]; then
	constrainMemSingleUser
    elif [ -n "$USER_INIT" ]; then
	constrainUser "SWAPPINESS"
    else
	setJobMem "$SWAPPINESS" "memory.swappiness"
	setStepMem "$SWAPPINESS" "memory.swappiness"
    fi
fi
