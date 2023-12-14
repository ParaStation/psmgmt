#!/bin/bash

# (C) 2023	ParTec AG, Munich

# Read current power sensors and integrate across time
#  read list of power sensor to be integrated from host object, key Bmc.PowerSensorList
# See tickets jwt:#19640, psc:#411

PATH=$PATH:/opt/parastation/bin

declare TIMEOUT=15				# ipmitool timeout

declare -i timestamp
declare -i now
declare -i dt

declare -a Sensors=()
declare SensorsFile EnergyFile SDRcacheFile

SensorsFile="/dev/shm/$(basename "$0").sensors"
EnergyFile="/dev/shm/$(basename "$0").current_energy"
SDRcacheFile="/dev/shm/$(basename "$0").sdr.cache"

if [[ -f $SensorsFile ]]
then
	mapfile -t Sensors <"$SensorsFile" || exit 1
else
	# no sensornames cache file found, look for entry in host object
	# shellcheck disable=SC2046
	eval Sensors=$(psconfig -b getList host:"${HOSTNAME%%.*}" Bmc.PowerSensorList)

	# create sensornames cache file
	[[ -n  "${Sensors[0]}" ]] &&
		printf "%s\n" "${Sensors[@]}" >"$SensorsFile"
fi

[[ -z "${Sensors[0]}" ]] && 
	exit 1

[[ -f $SDRcacheFile ]] ||
	ipmitool sdr dump "$SDRcacheFile" >/dev/null

# start with 0 energy at current time
[[ -f "$EnergyFile" ]] || 
	printf "%s\n%(%s)T\n" "0" "-1" >"$EnergyFile"

set -o pipefail
power=$(pstimeout $TIMEOUT ipmitool -S "$SDRcacheFile" sensor reading "${Sensors[@]}" | 
	awk 'BEGIN {power=0}; {power+=$NF}; END {printf ("%d", power+0.5)}') || 
	exit 1

# integrate
mapfile -t <"$EnergyFile"
energy=${MAPFILE[0]}
timestamp=${MAPFILE[1]}

printf -v now "%(%s)T" -1
(( dt=now-timestamp ))
(( energy=energy+power*dt ))

printf "%s\n%(%s)T\n" "$energy" -1 >"$EnergyFile"

echo power:"$power" energy:"$energy"
