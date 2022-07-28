#!/bin/bash

# Script to read MEGWARE energy meter data
#
# needed psaccount.conf example configuration:
#
# POWER_UNIT = mW
# ENERGY_SCRIPT = megware_energymeter.sh
# ENERGY_SCRIPT_POLL = 30
#
# The energy monitor script is supposed to output one line with the format
#
# power:num energy:num


sem_path=/sys/devices/platform/

power=0
energy=0

for sem in $sem_path/sem.*; do

        [ -d $sem ] || { echo power:-1 energy:-1; exit ; }

        power_sem=$(cat $sem/power_mw)
        energy_sem=$(cat $sem/energy_j)
        power=$((power+power_sem))
        energy=$((energy+energy_sem))

done

echo power:$power energy:$energy
