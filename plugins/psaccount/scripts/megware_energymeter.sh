#!/bin/bash

# script to read the MEGWARE energy meter

# needed psaccount.conf example configuration:
#
# POWER_UNIT = mW
# ENERGY_SCRIPT = megware_energymeter.sh
# ENERGY_SCRIPT_POLL = 30


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
