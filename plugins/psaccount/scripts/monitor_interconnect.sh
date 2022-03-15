#!/bin/bash

PORT=1

[ -n "$INFINIBAND_OFED_PORT" ] && PORT="$INFINIBAND_OFED_PORT"

perfquery -x -P $PORT | grep -P "PortSelect|PortXmitData|PortRcvData|PortXmitPkts|PortRcvPkts" | sort | sed -e 's/\.//g' -e 's/^Port//g' | tr '\n' ' '
