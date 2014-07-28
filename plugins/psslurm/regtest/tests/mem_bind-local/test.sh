#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

gcc -DMEM_MASK=1 prog.c -o output-${JOB_NAME}/prog.exe -lnuma
srun -n 2 --mem_bind=local --ntasks-per-socket=1 output-${JOB_NAME}/prog.exe

