#!/bin/bash

SRUN="srun"
if ! [[ "x" == "x${PSTEST_PARTITION}" ]]; then
	SRUN="${SRUN} --partition ${PSTEST_PARTITION}"
fi
if ! [[ "x" == "x${PSTEST_RESERVATION}" ]]; then
	SRUN="${SRUN} --reservation ${PSTEST_RESERVATION}"
fi
if ! [[ "x" == "x${PSTEST_RESERVATION}" ]]; then
	SRUN="${SRUN} --qos ${PSTEST_RESERVATION}"
fi
if ! [[ "x" == "x${PSTEST_ACCOUNT}" ]]; then
	SRUN="${SRUN} --account ${PSTEST_ACCOUNT}"
fi

ULIMA=1384
ULIMB=1723

ulimit -u

ulimit -u ${ULIMA}
ulimit -u
${SRUN} -N 1 -n 1 --propagate=NPROC ./ulimit.sh

ulimit -u ${ULIMB}
ulimit -u
${SRUN} -N 1 -n 1 --propagate=NPROC ./ulimit.sh

