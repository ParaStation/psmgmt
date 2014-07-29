#!/bin/bash

sleep 1

printf "%d %d\n" $(sacct -j ${SLURM_JOB_ID} | grep RUNNING | wc -l) $(sacct -j ${SLURM_JOB_ID} | grep COMPLETED | wc -l)

srun -N 1 -n 1 true

sleep 1

printf "%d %d\n" $(sacct -j ${SLURM_JOB_ID} | grep RUNNING | wc -l) $(sacct -j ${SLURM_JOB_ID} | grep COMPLETED | wc -l)

srun -N 1 -n 1 true

sleep 1

printf "%d %d\n" $(sacct -j ${SLURM_JOB_ID} | grep RUNNING | wc -l) $(sacct -j ${SLURM_JOB_ID} | grep COMPLETED | wc -l)

exit 0

