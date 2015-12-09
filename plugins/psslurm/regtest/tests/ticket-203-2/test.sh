#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

gcc -fopenmp prog.c -o output-${JOB_NAME}/prog.exe 

(
	ulimit -t 10
	env OMP_NUM_THREADS=8 srun -n 2 --cpu_bind=none --propagate=CPU output-${JOB_NAME}/prog.exe
)

