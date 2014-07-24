#!/bin/bash

RES=""
if ! [[ "x" == "x${PSTEST_RESERVATION}" ]]; then
	RES="--reservation ${PSTEST_RESERVATION}"
fi

ulimit -u

ulimit -u 4096
ulimit -u
srun -N 1 -n 1 --propagate=NPROC -p ${PSTEST_PARTITION} ${RES} ./ulimit.sh

ulimit -u 2048
ulimit -u
srun -N 1 -n 1 --propagate=NPROC -p ${PSTEST_PARTITION} ${RES} ./ulimit.sh

