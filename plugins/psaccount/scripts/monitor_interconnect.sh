#!/bin/bash
#
# The interconnect monitor script is supposed to output one line with the format
#
# "RcvData:num RcvPkts:num Select:num XmitData:num XmitPkts:num"
#
# RcvData = number of bytes received
# RcvPkts = number of packets received
# Select = the (ofed) port monitored
# XmitData = number of bytes sent
# XmitPkts = number of packets sent
#
# An optional environment variable INTERCONNECT_TYPE may specify which
# interconnect to monitor.
#
# If infiniband is monitored an optional environment variable INFINIBAND_OFED_PORT
# may specify the ofed port to monitor.
#

PORT=1

[ -n "$INFINIBAND_OFED_PORT" ] && PORT="$INFINIBAND_OFED_PORT"

perfquery -x -P $PORT |
    grep -P "PortSelect|PortXmitData|PortRcvData|PortXmitPkts|PortRcvPkts" |
    sort | sed -e 's/\.//g' -e 's/^Port//g' | tr '\n' ' '
