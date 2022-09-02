#!/bin/bash
#
# The energy monitor script is supposed to output one line with the format
#
# power:num energy:num
#
# power = current power usage
# energy = used energy in joules
#
# An optional environment variable ENERGY_TYPE may specify which
# facility to use for energy monitoring.
#
if [ "$ENERGY_TYPE" == "acct_gather_energy/rapl" ]; then
    # read rapl energy values from /sys filesystem
    power=0; energy=0
    for next in /sys/class/powercap/intel-rapl/intel-rapl\:*/energy_uj; do
	val=$(cat $next)
	# convert micro joule to joule and add all packages
	energy=$(($energy + $val/1000000))
    done
else
    power=0; energy=0
fi

echo power:$power energy:$energy
