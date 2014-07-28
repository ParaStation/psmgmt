#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

gcc -DCPU_MASK=1 prog.c -o output-${JOB_NAME}/prog.exe 
srun -n 2 --cpu_bind=map_ldom:0,1 output-${JOB_NAME}/prog.exe

