#!/bin/bash

CommandPath=$(dirname "$0")

LOADER="$CommandPath/bpf_device_loader"
if [[ ! -x $LOADER ]]; then
    echo "unable to find bpf_device_loader: $LOADER"; exit 1;
fi

PID=$1
if [[ -z $PID ]]; then
    echo "missing PID to show BPF constraints";
    echo "usage: $0 PID"; exit 1;
fi

if ! ps -p "$PID" > /dev/null 2>&1; then
    echo "$PID is not a valid PID"; exit 1;
fi

if ! ls /sys/fs/bpf > /dev/null 2>&1; then
    echo "no permission to access BPF information"; exit 1;
fi

CGROUP=$(cat /proc/"$PID"/cgroup)

if [[ $CGROUP =~ /user-([^/]+)/job-([0-9]+)/step-([0-9]+)/tasks ]]; then
    USERNAME="${BASH_REMATCH[1]}"
    JOBID="${BASH_REMATCH[2]}"
    STEPID="${BASH_REMATCH[3]}"
elif [[ $CGROUP =~ /user-([^/]+)/job-([0-9]+)/tasks ]]; then
    USERNAME="${BASH_REMATCH[1]}"
    JOBID="${BASH_REMATCH[2]}"
elif [[ $CGROUP =~ /user-([^/]+)/tasks ]]; then
    USERNAME="${BASH_REMATCH[1]}"
else
    echo "$PID is not constrained by a psid cgroup"
    exit 0
fi

function print_access() {
    local line=$1
    if [[ $line =~ Access\ to\ device\ ([0-9]+):([0-9]+)\ is\ (allowed|denied) ]]; then
	major="${BASH_REMATCH[1]}"
	minor="${BASH_REMATCH[2]}"
	status="${BASH_REMATCH[3]}"

	# find device name
	device=$(find /dev -type b -o -type c -exec stat -c "%t %T %n" {} + | \
	    awk -v maj="$major" -v min="$minor" '$1 == maj && $2 == min {print $3}')

	if [ -n "$device" ]; then
	    echo "Access to device $device ($major:$minor) is $status"
	else
	    echo "$line"
	fi
    else
	echo "$line"
    fi
}

if [[ -n $USERNAME ]]; then
    echo "user $USERNAME devices:"

    while IFS= read -r line; do
	print_access "$line"
    done < <("$LOADER" -i "USER_${USERNAME}" -s)
fi

if [[ -n $JOBID ]]; then
    echo ""
    echo "job $JOBID devices:"

    while IFS= read -r line; do
	print_access "$line"
    done < <("$LOADER" -i "JOB_${JOBID}" -s)
fi

if [[ -n $STEPID && -n $JOBID ]]; then
    echo ""
    echo "step $JOBID:$STEPID devices:"

    while IFS= read -r line; do
	print_access "$line"
    done < <("$LOADER" -i "STEP_${JOBID}_${STEPID}" -s)
fi
