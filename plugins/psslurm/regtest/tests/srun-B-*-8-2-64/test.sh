#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

gcc prog.c -o output-${JOB_NAME}/prog.exe
srun -B \*:8:2 -n 64 output-${JOB_NAME}/prog.exe

