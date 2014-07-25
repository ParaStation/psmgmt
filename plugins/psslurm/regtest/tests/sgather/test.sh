#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

RFILE=/tmp/slurm-${SLURM_JOB_ID}.txt
LFILE=output-${JOB_NAME}/$(basename $RFILE)


srun -n 1 /bin/bash -c "echo OK > ${RFILE}" >/dev/null 2>/dev/null
echo $?

srun -n 1 /bin/bash -c "stat ${RFILE}" >/dev/null 2>/dev/null
echo $?

sgather ${RFILE} ${LFILE}

srun -n 1 /bin/bash -c "stat ${LFILE}" >/dev/null 2>/dev/null
echo $?

exit 0

