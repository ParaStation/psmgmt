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
# daemon psid, i.e. typically with root permission! Thus, special care
# has to be taken when changing this script.
#
# This script will be called by the jail plugin via system() and get
# the process ID of the process to be jailed as an argument.

IS_INIT="false"

function initMem()
{
    [ -n "$SLURM_USER" ] && USER=$SLURM_USER

    [ -n "$USER" ] || {
	    elog "no user env variable for job $SLURM_JOBID"
    }

    CG_USER="$BASE/$PREFIX-$USER"
    mdsave "$CG_USER"

    [ -n "$SLURM_JOBID" ] || elog "no jobid for pid $CHILD user $USER"

    CG_JOB="$CG_USER/job-$SLURM_JOBID"
    mdsave "$CG_JOB"

    [ -n "$SLURM_STEPID" ] && {
	    CG_STEP="$CG_JOB/step-$SLURM_STEPID"
	    mdsave "$CG_STEP"
    }

    dlog "adding child $CHILD to $CG_JOB/tasks"
    echo "$CHILD" > $CG_JOB/tasks

    # step cgroup
    [ -n "$SLURM_STEPID" ] && {
	dlog "adding child $CHILD to $CG_STEP/tasks"
	echo "$CHILD" > $CG_STEP/tasks
    }
    IS_INIT="true"
}

function setMem()
{
    local LIMIT=$1

    [ "$IS_INIT" == "false" ] && initMem;

    # job cgroup
    dlog "$__PSJAIL_JOB_MEM_ALLOC > $CG_JOB/$LIMIT JOB: $SLURM_JOBID STEP:$SLURM_STEPID"
    echo "$__PSJAIL_JOB_MEM_ALLOC" > $CG_JOB/$LIMIT

    # step cgroup
    [ -n "$SLURM_STEPID" ] && {
	dlog "$__PSJAIL_STEP_MEM_ALLOC > $CG_STEP/$LIMIT"
        echo "$__PSJAIL_STEP_MEM_ALLOC" > $CG_STEP/$LIMIT

	echo "$__PSJAIL_STEP_MEM_ALLOC" > $CG_STEP/$LIMIT
    }
}

if [ "$__PSJAIL_CONSTRAIN_KMEM" == "no" ]; then
    dlog "constrain KMEM *disabled*"
else
    dlog "constrain KMEM enabled"
    setMem "memory.kmem.limit_in_bytes"
fi

if [ "$__PSJAIL_CONSTRAIN_RAM" == "no" ]; then
    dlog "constrain RAM *disabled*"
else
    dlog "constrain RAM enabled"
    setMem "memory.limit_in_bytes"
fi

if [ "$__PSJAIL_CONSTRAIN_SWAP" == "no" ]; then
    dlog "constrain SWAP *disabled*"
else
    dlog "constrain SWAP enabled"
    setMem "memory.memsw.limit_in_bytes"
fi
