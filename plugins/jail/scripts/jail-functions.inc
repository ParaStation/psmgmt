#!/bin/bash
#
# ParaStation
#
# Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2022-2026 ParTec AG, Munich
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
	return 1
    }
}

function getSharedUserLock {
    exec 101>"/dev/shm/psjail-$user"
    flock -s 101
}

function getExclusiveUserLock {
    exec 101>"/dev/shm/psjail-$user"
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

# True when this cgroup or one of its descendants still has a process
function cgPopulated {
    local dir=$1
    local events

    [[ -d $dir ]] || return 1
    if events=$(cat "$dir/cgroup.events" 2>/dev/null); then
	[[ $events == *"populated 1"* ]]
    else
	[[ -n $(cgPids "$dir") ]]
    fi
}

# Space-separated PIDs in this cgroup and its descendants
function cgPids {
    local dir=$1
    local child part pids=""

    if [[ -r $dir/cgroup.procs ]]; then
	part=$(tr '\n' ' ' < "$dir/cgroup.procs")
	pids+="$part"
    fi
    for child in "$dir"/*/; do
	child=${child%/}
	[[ -d $child && ! -L $child ]] || continue
	part=$(cgPids "$child")
	[[ -n $part ]] && pids+="$part "
    done
    read -r pids <<< "$pids"
    printf '%s\n' "$pids"
}

# Send signal to every PID in this cgroup and its descendants
function cgSignalTree {
    local dir=$1
    local sig=$2
    local pid

    for pid in $(cgPids "$dir"); do
	[[ -d /proc/$pid ]] || continue
	dlog "send signal $sig to PID $pid"
	kill -"$sig" "$pid" 2>/dev/null
    done
}

# Kill every process in this cgroup and its descendants via cgroup.kill.
# Writing cgroup.kill again on every retry catches a task migrated in
# afterwards.
function cgFatalKill {
    local dir=$1
    local -i try=0
    local -i limit=${MAX_KILL_RETRY:-10}

    while cgPopulated "$dir"; do
	(( try > limit )) && return 1
	if ! echo 1 > "$dir/cgroup.kill" 2>/dev/null; then
	    mlog "write 1 to $dir/cgroup.kill failed"
	    return 1
	fi
	dlog "cgroup.kill $dir"
	try+=1
	sleep 0.1
    done
    return 0
}

# Cgroups whose tasks survived cgFatalKill. jail-term calls
# killTasks once per term module and from jail-main.sh,
# so we do not wait for the same survivors again.
declare -A KILL_GAVE_UP=()

function killTasks {
    local DIR=$1
    local caller
    caller=$(caller 0 | awk '{print $2}')

    # Only touch cgroups below $CGROUP_BASE/$PREFIX.
    if [[ $(realpath -ms "$DIR") != "$CGROUP_BASE/$PREFIX/"?* ]]; then
	mlog "refuse to kill tasks in '$DIR'"
	return 1
    fi
    [[ -d $DIR ]] || {
	[[ $CHILD != -1 ]] && dlog "$caller: no directory $DIR to kill tasks"
	return
    }

    if [[ -n ${KILL_GAVE_UP[$DIR]-} ]]; then
	dlog "$caller: already gave up on $DIR"
	return 1
    fi

    # cgroup.kill and cgPids cover the whole subtree, so this also kills
    # the steps of a job and the jobs of a user
    dlog "$caller: dir $DIR"
    if [[ ${TERM_DELAY:-0} != 0 ]] && cgPopulated "$DIR"; then
	cgSignalTree "$DIR" "$TERM_SIGNAL"
	sleep "$TERM_DELAY"
    fi
    if ! cgFatalKill "$DIR"; then
	KILL_GAVE_UP[$DIR]=1
	mlog "could not kill remaining processes in $DIR"
	return 1
    fi

    # Remove the tree leaves first, rmdir refuses non-empty cgroups
    dlog "$caller: rmdir $DIR"
    find "$DIR" -type d -delete 2>/dev/null
    [[ ! -d $DIR ]] || mlog "$caller: rmdir $DIR failed"
}

function initJailEnv {
    unset LANGUAGE
    export LANG=C

    export ALLOC_LIST=$__PSJAIL_ALLOC_LIST
    export JOBID=$__PSJAIL_JOBID
    export STEPID=$__PSJAIL_STEPID

    # ADD_TO_USER_CGROUP is used to add the process only to the user cgroup.
    # This is particularly important for the PMIx server, since it serves multiple
    # jobs/steps and should only be terminated when the user has no remaining
    # allocations on the node.
    #
    # pspam is used to allow SSH access to the nodes as soon as the prologue has
    # finished. The SSH process also cannot be tied to a specific allocation.
    # Like the PMIx server, it should only be terminated when the user has no
    # allocations left on the node.
    #
    # Additionally, no cgroup will be set up. A correctly configured user cgroup
    # is expected to already exist. Otherwise, the jail process will fail.
    export ADD_TO_USER_CGROUP=$__PSJAIL_ADD_TO_USER_CGROUP

    # USER_INIT is used for processes that are expected to be tied at least
    # to a job or a job/step combination. It will build the cgroup hierarchy
    # if it does not already exist. It is assumed to have all the information
    # required for this purpose (e.g. allocated cores).
    export USER_INIT=$__PSJAIL_USER_INIT

    if [[ -n $__PSJAIL_USER ]]; then
	export user=$__PSJAIL_USER
    elif [[ -n $ADD_TO_USER_CGROUP ]]; then
	export user=$ADD_TO_USER_CGROUP
    else
	# used by psiforwarder jail hook
	export user=$USER
    fi

    # map core pinning variables
    export JOB_CPUS=$__PSJAIL_JOB_CPUS
    export STEP_CPUS=$__PSJAIL_STEP_CPUS
    export CONSTRAIN_CORES=$__PSJAIL_CONSTRAIN_CORES
    export ENFORCE_STEP_CORES=$__PSJAIL_ENFORCE_STEP_CORES

    # map various memory pinning variables
    export JOB_RAM_SOFT=$__PSJAIL_JOB_RAM_SOFT
    export STEP_RAM_SOFT=$__PSJAIL_STEP_RAM_SOFT

    export JOB_RAM_HARD=$__PSJAIL_JOB_RAM_HARD
    export STEP_RAM_HARD=$__PSJAIL_STEP_RAM_HARD

    export JOB_SWAP=$__PSJAIL_JOB_SWAP
    export STEP_SWAP=$__PSJAIL_STEP_SWAP

    export SWAPPINESS=$__PSJAIL_SWAPPINESS

    export CONSTRAIN_RAM=$__PSJAIL_CONSTRAIN_RAM
    export CONSTRAIN_SWAP=$__PSJAIL_CONSTRAIN_SWAP

    export CONSTRAIN_DEVICES=$__PSJAIL_CONSTRAIN_DEVICES

    [[ -z $TERM_SIGNAL ]] && export TERM_SIGNAL=15

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
    local CGROUPS

    [[ -n $CGROUP_BASE && -n $PREFIX && $PREFIX != */*
       && $PREFIX != . && $PREFIX != .. ]] || \
	elog "refuse CGROUP_BASE '$CGROUP_BASE' and PREFIX '$PREFIX'"
    CGROUPS="$CGROUP_BASE/$PREFIX"
    [[ -d $CGROUPS ]] || return 0

    dlog "cleanup v2 $CGROUPS"

    cgFatalKill "$CGROUPS"
    find "$CGROUPS" -type d -delete 2>/dev/null
    [[ -d $CGROUPS ]] && elog "unable to remove $CGROUPS"
}

# vim: set filetype=sh ts=8 sw=4 tw=0 sts=4 noet :
