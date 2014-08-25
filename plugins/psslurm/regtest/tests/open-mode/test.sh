#!/bin/bash

echo "1" >   output-${SLURM_JOB_NAME}/slurm-${SLURM_JOB_ID}-A.out
srun -n 1 -o output-${SLURM_JOB_NAME}/slurm-${SLURM_JOB_ID}-A.out --open-mode=append   /bin/bash -c "echo 2"

echo "1" >   output-${SLURM_JOB_NAME}/slurm-${SLURM_JOB_ID}-B.out
srun -n 1 -o output-${SLURM_JOB_NAME}/slurm-${SLURM_JOB_ID}-B.out --open-mode=truncate /bin/bash -c "echo 2"

