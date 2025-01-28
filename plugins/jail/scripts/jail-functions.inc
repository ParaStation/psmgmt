#!/bin/bash
#
# ParaStation
#
# Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022-2025 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# This script is executed with the same permissions as the ParaStation
# daemon psid, i.e. typically with root permissions! Thus, special care
# has to be taken when changing this script.

modName=${modName-undef}
CGROUP_VERSION=${CGOUP_VERSION-undef}
CHILD=${CHILD-0}

function mlog {
    local caller
    caller=$(caller 0 | awk '{print $2}')
    if [[ $LOG_TO_SYSLOG == 1 ]]; then
	logger --tag "psid-jail-$modName.$CGROUP_VERSION"  "$caller: $1"
    else
	echo "$(date) $modName.$CGROUP_VERSION-[$CHILD]: $caller: $1"
    fi
}

function dlog {
    [[ $DEBUG == 1 ]] || return;
    local caller
    caller=$(caller 0 | awk '{print $2}')
    if [[ $LOG_TO_SYSLOG == 1 ]]; then
	logger --tag "psid-jail-$modName.$CGROUP_VERSION"  "$caller: $1"
    else
	echo "$(date) $modName.$CGROUP_VERSION-[$CHILD]: $caller: $1"
    fi
}

function elog {
    local caller
    caller=$(caller 0 | awk '{print $2}')
    if [[ $LOG_TO_SYSLOG == 1 ]]; then
	logger --tag "psid-jail-$modName.$CGROUP_VERSION"  "$caller: $1"
    else
	echo "$(date) $modName.$CGROUP_VERSION-[$CHILD]: $caller: $1"
    fi
    exit 1
}

function cgWrite {
    local caller
    caller=$(caller 0 | awk '{print $2}')
    echo 2>/dev/null "$1" > "$2" || {
	mlog "$caller: write $1 to $2 failed"
    }
}

function getExclusiveUserLock {
    exec 101>"/dev/shm/psjail-$USER"
    flock -x 101
}

function rmUserLock {
    flock -u 101
}

function mdsave {
    local DIR=$1
    local caller
    caller=$(caller 0 | awk '{print $2}')
    [[ -d $DIR ]] || {
	mkdir "$DIR" 2>/dev/null
	[[ -d $DIR ]] || {
	    elog "$caller: mkdir($DIR) failed"
	}
    }
}

function rmDirLogged {
    local DIR=$1
    local caller
    caller=$(caller 0 | awk '{print $2}')
    [[ -d $DIR ]] && {
	rmdir "$DIR" 2>/dev/null || mlog "$caller: rmdir $DIR failed"
    }
}

function killTasks {
    local DIR=$1
    local caller
    caller=$(caller 0 | awk '{print $2}')
    [[ -d $DIR ]] || {
	[[ $CHILD != -1 ]] && dlog "$caller: no directory $DIR to kill tasks"
	return
    }

    dlog "$caller: dir $DIR"
    local TASKS="$DIR/tasks/cgroup.procs"

    local -i count=0
    local PIDS
    PIDS=$(cat "$TASKS" 2>/dev/null)

    while [[ -n $PIDS && $count -le $MAX_KILL_RETRY ]]; do
	if [[ $TERM_DELAY != 0 && $count == 0 ]]; then
	    for child in $PIDS; do
		[[ -d /proc/$child ]] || continue
		dlog "$caller($count): send signal $TERM_SIGNAL to PID $child"
		kill -"$TERM_SIGNAL" "$child"
	    done
	    sleep "$TERM_DELAY"
	    PIDS=$(cat "$TASKS" 2>/dev/null)
	fi
	for child in $PIDS; do
	    [[ -d /proc/$child ]] || continue
	    dlog "$caller($count): killing PID $child with signal $KILL_SIGNAL"
	    kill "-$KILL_SIGNAL" "$child"
	done

	(( count++ ))
	PIDS=$(cat "$TASKS" 2>/dev/null)
    done

    dlog "$caller: rmdir $DIR"
    rmDirLogged "$DIR/tasks"
    rmDirLogged "$DIR"
}

function killJob {
    local DIR=$1
    local caller
    caller=$(caller 0 | awk '{print $2}')
    [[ -d $DIR ]] || {
	[[ $CHILD != -1 ]] && dlog "$caller: no directory $DIR to kill tasks"
	return
    }

    dlog "kill steps in dir $DIR"

    local BASE=$DIR
    for i in "$BASE"/step-*/; do
	[[ -d $i ]] || break
	dlog "$caller: killTasks $i"
	killTasks "$i"
    done

    dlog "$caller: kill tasks of $DIR"
    killTasks "$DIR"
}

function killUser {
    local DIR=$1
    local caller
    caller=$(caller 0 | awk '{print $2}')
    [[ -d $DIR ]] || {
	[[ $CHILD != -1 ]] && dlog "$caller: no directory $DIR to kill tasks"
	return
    }

    dlog "kill jobs in dir $DIR"
    local BASE=$DIR
    for i in "$BASE"/job-*/; do
	[[ -d $i ]] || break
	dlog "$caller: killJob $i"
	killJob "$i"
    done

    dlog "$caller: kill tasks of $DIR"
    killTasks "$DIR"
}

function initJailEnv {
    unset LANGUAGE
    export LANG=C

    export JOBID=$__PSJAIL_JOBID
    export STEPID=$__PSJAIL_STEPID

    export ADD_USER_TO_CGROUP=$__PSJAIL_ADD_USER_TO_CGROUP
    export ALLOC_LIST=$__PSJAIL_ALLOC_LIST
    export USER_INIT=$__PSJAIL_USER_INIT
    [[ -n $__PSJAIL_USER ]] && export USER=$__PSJAIL_USER
    [[ -z $USER && -n $ADD_USER_TO_CGROUP ]] && {
	export USER=$ADD_USER_TO_CGROUP
    }

    # map core pinning variables
    export JOB_CPUS=$__PSJAIL_JOB_CPUS
    export STEP_CPUS=$__PSJAIL_STEP_CPUS
    export CONSTRAIN_CORES=$__PSJAIL_CONSTRAIN_CORES

    # map various memory pinning variables
    export JOB_RAM_SOFT=$__PSJAIL_JOB_RAM_SOFT
    export STEP_RAM_SOFT=$__PSJAIL_STEP_RAM_SOFT

    export JOB_RAM_HARD=$__PSJAIL_JOB_RAM_HARD
    export STEP_RAM_HARD=$__PSJAIL_STEP_RAM_HARD

    export JOB_KMEM=$__PSJAIL_JOB_KMEM
    export STEP_KMEM=$__PSJAIL_STEP_KMEM

    export JOB_SWAP=$__PSJAIL_JOB_SWAP
    export STEP_SWAP=$__PSJAIL_STEP_SWAP

    export SWAPPINESS=$__PSJAIL_SWAPPINESS

    export CONSTRAIN_RAM=$__PSJAIL_CONSTRAIN_RAM
    export CONSTRAIN_KMEM=$__PSJAIL_CONSTRAIN_KMEM
    export CONSTRAIN_SWAP=$__PSJAIL_CONSTRAIN_SWAP

    export CONSTRAIN_DEVICES=$__PSJAIL_CONSTRAIN_DEVICES

    [[ -z $TERM_SIGNAL ]] && export TERM_SIGNAL=15

    [[ -z $KILL_SIGNAL ]] && export KILL_SIGNAL=9

    [[ -z $MIN_VALID_MEMORY_LIMIT ]] && export MIN_VALID_MEMORY_LIMIT=100

    export CGROUP_VERSION=$__PSJAIL_CGROUP_VERSION
    if [[ -z $CGROUP_VERSION || $CGROUP_VERSION == autodetect ]]; then
	# auto detect cgroup version
	if [[ -f $CGROUP_BASE/cgroup.controllers ]]; then
	    export CGROUP_VERSION="v2"
	else
	    elog "unable to detect cgroup version"
	fi
    elif [[ $CGROUP_VERSION == "v1" ]]; then
	elog "unsupported cgroup version $CGROUP_VERSION"
    fi
}

function assertController {
    local CONTROLLER=$1
    local CTL="$CGROUP_BASE/cgroup.controllers"

    [[ -z $CONTROLLER ]] && elog "no controller given"
    AVAIL=$(cat "$CTL")
    if [[ ! " $AVAIL " == *" $CONTROLLER "* ]]; then
	elog "controller $CONTROLLER not available in $CTL"
    fi
}

function enableSingleController {
    local CONTROLLER=$1
    local DIR=$2
    local SUBCTL="cgroup.subtree_control"

    [[ -z $CONTROLLER ]] && elog "no controller given"
    [[ -z $DIR ]] && elog "no directory given"

    ENABLED=$(cat "$DIR/$SUBCTL")
    if [[ ! " $ENABLED " == *" $CONTROLLER "* ]]; then
	cgWrite "+$CONTROLLER" "$DIR/$SUBCTL"
	dlog "enable: cgWrite +$CONTROLLER $DIR/$SUBCTL"
    fi
}

function enableControllers {
    local DIR=$1
    [[ -z $DIR ]] && elog "no directory given"

    dlog "enable controllers in $DIR"
    mdsave "$DIR"
    for controller in ${CGROUP_CONTROLLER//,/$IFS}; do
	enableSingleController "$controller" "$DIR"
    done
    mdsave "$DIR/tasks"
}

function cleanupCgroups {
    CGROUPS="$CGROUP_BASE/$PREFIX"
    dlog "cleanup v2 $CGROUPS"
    for user in "$CGROUPS"/user-*; do
	for job in "$user"/job-*; do
	    for step in "$job"/step-*; do
		xargs -r -n1 kill -9 2>/dev/null < "$step/tasks/cgroup.procs"
		rmdir -p "$step/tasks" 2>/dev/null
		rmdir -p "$step" 2>/dev/null
	    done
	    xargs -r -n1 kill -9 2>/dev/null < "$job/tasks/cgroup.procs"
	    rmdir -p "$job/tasks" 2>/dev/null
	    rmdir -p "$job" 2>/dev/null
	done
	xargs -r -n1 kill -9 2>/dev/null < "$user/tasks/cgroup.procs"
	rmdir -p "$user/tasks" 2>/dev/null
	rmdir -p "$user" 2>/dev/null
    done
    rmdir -p "$CGROUPS" 2>/dev/null
    [ -d "$CGROUPS" ] && { elog "unable to remove $CGROUPS"; }
}

# vim: set filetype=sh ts=8 sw=4 tw=0 sts=4 noet :
