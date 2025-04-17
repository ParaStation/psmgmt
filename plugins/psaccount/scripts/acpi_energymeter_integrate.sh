#!/bin/bash
#
# Read sensor data from the hardware monitoring (hwmon) subsystem
#
# The energy monitor script is supposed to output one line with the format
#
# power:num energy:num

declare -i timestamp
declare -i now
declare -i dt

EnergyFile="/dev/shm/$(basename "$0").current_energy"

# start with 0 energy at current time
[[ -f "$EnergyFile" ]] ||
    printf "%s\n%(%s)T\n" "0" "-1" >"$EnergyFile"

power=0

# power1_average is the power consumptions over the set _interval_ in microwatts
for sensor in /sys/class/hwmon/hwmon*/device/power1_average
do
    value=$(<"$sensor")
    power=$((power + value))
done

power=$((power / 10**6))

# integrate
mapfile -t <"$EnergyFile"
energy=${MAPFILE[0]}
timestamp=${MAPFILE[1]}

printf -v now "%(%s)T" -1
(( dt=now-timestamp ))
(( energy=energy+power*dt ))

printf "%s\n%(%s)T\n" "$energy" -1 >"$EnergyFile"

echo power:"$power" energy:"$energy"
