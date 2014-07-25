#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

RFILE=/tmp/slurm-${SLURM_JOB_ID}.txt
LFILE=output-${JOB_NAME}/$(basename $RFILE)


echo OK > ${LFILE}

srun -n 1 /bin/bash -c "stat ${RFILE}" >/dev/null 2>/dev/null
echo $?

sbcast ${LFILE} ${RFILE}

srun -n 1 /bin/bash -c "stat ${RFILE}" >/dev/null 2>/dev/null
echo $?

cat ${LFILE}
srun -n 1 /bin/bash -c "cat ${RFILE}"

exit 0

