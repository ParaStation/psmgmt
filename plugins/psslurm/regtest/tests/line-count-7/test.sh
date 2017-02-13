#!/bin/bash

JOB_NAME=$(scontrol show job -o ${SLURM_JOB_ID} | python2 -c 'import sys ; import os; d = dict([(x[0], "=".join(x[1:])) for x in map(lambda u: u.split("="), sys.stdin.read().split())]) ; x = d["Name"] if "Name" in d.keys() else d["JobName"] ; print(x)')

srun -n 4 -t 1 -e output-${JOB_NAME}/slurm-1.err -o output-${JOB_NAME}/slurm-1-%t.out -i input.txt cat
echo $?

sleep 1

stat output-${JOB_NAME}/slurm-1-0.out
stat output-${JOB_NAME}/slurm-1-1.out
stat output-${JOB_NAME}/slurm-1-2.out
stat output-${JOB_NAME}/slurm-1-3.out

srun -n 4 -t 1 -e output-${JOB_NAME}/slurm-2.err -o output-${JOB_NAME}/slurm-2-%t.out -i output-${JOB_NAME}/slurm-1-%t.out cat -
echo $?

exit 0

