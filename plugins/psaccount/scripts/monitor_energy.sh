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
# Additional environment variables may be available dependent on
# the different energy types

if [ "$ENERGY_TYPE" == "acct_gather_energy/rapl" ]; then
    # read rapl energy values from /sys filesystem
    power=0; energy=0
    for next in /sys/class/powercap/intel-rapl/intel-rapl\:*/energy_uj; do
	val=$(cat $next)
	# convert micro joule to joule and add all packages
	energy=$(($energy + $val/1000000))
    done

elif [ "$ENERGY_TYPE" == "acct_gather_energy/ipmi" ]; then
    # read ipmi data
    # optional environment variables include
    # IPMI_FREQUENCY, IPMI_ADJUSTMENT, IPMI_POWER_SENSORS,
    # IPMI_USERNAME, IPMI_PASSWORD

    power=0; energy=0
else
    power=0; energy=0
fi

echo power:$power energy:$energy
