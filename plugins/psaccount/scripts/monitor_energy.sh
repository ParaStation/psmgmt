#!/bin/bash
#
# The energy monitor script is supposed to output one line with the format
#
# power:num energy:num
#
# power = current power usage
# energy = used energy
#
# An optional environment variable ENERGY_TYPE may specify which
# facility to use for energy monitoring.
#

echo power:$power energy:$energy
