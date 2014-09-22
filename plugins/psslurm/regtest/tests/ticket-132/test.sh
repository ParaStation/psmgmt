#!/bin/bash

RES=""
if ! [[ "x" == "x${PSTEST_RESERVATION}" ]]; then
	RES="--reservation ${PSTEST_RESERVATION}"
fi

NCPUS=$((2*${PSTEST_PARTITION_CPUS}))

gcc prog.c -o ${PSTEST_OUTDIR}/prog.exe -lelf
${PSTEST_OUTDIR}/prog.exe ${PSTEST_OUTDIR}/mpir_proctable.txt $(which srun) -p ${PSTEST_PARTITION} ${RES} -t 2 -n ${NCPUS} sleep 10

