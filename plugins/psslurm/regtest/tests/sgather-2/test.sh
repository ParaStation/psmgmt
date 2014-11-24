#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

RFILE=/tmp/slurm-${SLURM_JOB_ID}.txt
LFILE=output-${JOB_NAME}/$(basename $RFILE)


srun -n 2 --ntasks-per-node 1 /bin/bash -c "echo OK > ${RFILE}" >/dev/null 2>/dev/null
echo $?

srun -n 2 --ntasks-per-node 1 /bin/bash -c "stat ${RFILE}" >/dev/null 2>/dev/null
echo $?

sgather ${RFILE} ${LFILE}

srun -n 2 --ntasks-per-node 1 /bin/bash -c "stat ${RFILE}" >/dev/null 2>/dev/null
echo $?

for H in $(scontrol show hostnames ${SLURM_JOB_NODELIST}) ; do
	stat ${LFILE}.${H} >/dev/null 2>/dev/null
	echo $?
done

exit 0

