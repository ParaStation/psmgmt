#!/bin/bash

JOB_NAME=$(scontrol show job -o ${SLURM_JOB_ID} | python2 -c 'import sys ; import os; d = dict([(x[0], "=".join(x[1:])) for x in map(lambda u: u.split("="), sys.stdin.read().split())]) ; x = d["Name"] if "Name" in d.keys() else d["JobName"] ; print(x)')

RFILE=/tmp/slurm-${SLURM_JOB_ID}.txt
LFILE=output-${JOB_NAME}/$(basename $RFILE)


srun -n 1 /bin/bash -c "echo OK > ${RFILE}" >/dev/null 2>/dev/null
echo $?

srun -n 1 /bin/bash -c "stat ${RFILE}" >/dev/null 2>/dev/null
echo $?

sgather ${RFILE} ${LFILE}

srun -n 1 /bin/bash -c "stat ${RFILE}" >/dev/null 2>/dev/null
echo $?

stat ${LFILE}.${SLURM_JOB_NODELIST} >/dev/null 2>/dev/null
echo $?

exit 0

