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

ulimit -u

ulimit -u 4096
ulimit -u
${SRUN} -N 1 -n 1 --propagate=NPROC ./ulimit.sh

ulimit -u 2048
ulimit -u
${SRUN} -N 1 -n 1 --propagate=NPROC ./ulimit.sh

