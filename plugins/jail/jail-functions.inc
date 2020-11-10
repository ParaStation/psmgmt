#!/bin/bash
#
# ParaStation
#
# Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# This script is executed with the same permissions as the ParaStation
# daemon psid, i.e. typically with root permission! Thus, special care
# has to be taken when changing this script.

function mlog {
	echo $(date): $1
}

function dlog {
	[ "$DEBUG" == "1" ] || return;
	echo $(date): $1
}

function elog {
	echo $(date): $1 >&2
	exit 1
}

function mdsave {
	local DIR=$1
	[ -d "$DIR" ] || {
		mkdir "$DIR"
		[ -d "$DIR" ] || {
			elog "mkdir($DIR) failed"
		}
	}
}

function killTasks {
	local DIR=$1
	[ -d "$DIR" ] || {
		elog "killTasks: no directory $DIR to kill tasks"
	}

	dlog "killTasks: dir $DIR"

	local -i count=0
	local PIDS=$(cat "$DIR/tasks")

	while [ -n "$PIDS" -a "$count" -le "$MAX_KILL_RETRY" ]; do
		for child in $PIDS; do
			CMD=$(cat /proc/$child/cmdline)
			dlog "killTasks($count): killing PID $child ($CMD) with signal $KILL_SIGNAL"
			kill -"$KILL_SIGNAL" $child
		done

		(( count++ ))
		PIDS=$(cat "$DIR/tasks")
	done

	dlog "killTasks: rmdir $DIR"
	rmdir "$DIR"
}

function killJob {
	local DIR=$1
	[ -d "$DIR" ] || {
		elog "killJob: no directory $DIR to kill tasks"
	}

	dlog "killJob: dir $DIR"

	local BASE="$DIR"
	for i in "$BASE"/step-*/; do
		[ -d "$i" ] || break
		dlog "killJob: killTasks $i"
		killTasks "$i"
	done

	dlog "killJob: rmdir $DIR"
	rmdir "$DIR"
}

