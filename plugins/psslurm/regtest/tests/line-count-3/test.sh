#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

srun -n 4 -t 1 -e output-${SLURM_JOB_NAME}/slurm-1.err -o output-${SLURM_JOB_NAME}/slurm-1-%t.out -i input.txt cat
echo $?

sleep 1

stat output-${SLURM_JOB_NAME}/slurm-1-0.out
stat output-${SLURM_JOB_NAME}/slurm-1-1.out
stat output-${SLURM_JOB_NAME}/slurm-1-2.out
stat output-${SLURM_JOB_NAME}/slurm-1-3.out

srun -n 4 -t 1 -e output-${SLURM_JOB_NAME}/slurm-2.err -o output-${SLURM_JOB_NAME}/slurm-2.out -i output-${SLURM_JOB_NAME}/slurm-1-%t.out cat -
echo $?

exit 0

