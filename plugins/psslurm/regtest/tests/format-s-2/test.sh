#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

for i in {0..7} ; do
	srun -N 4 -n 8 --ntasks-per-node=2 -o output-${JOB_NAME}/slurm-%j-%4s.out -e output-${JOB_NAME}/slurm-%j-%4s.err ./hostname.py ${i}
done

exit 0

