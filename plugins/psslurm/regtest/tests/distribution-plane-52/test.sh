#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

env PATH=/usr/local/bin:/bin:/usr/bin:/usr/local/sbin:/usr/sbin:/sbin /usr/bin/gcc prog.c -o output-${JOB_NAME}/prog.exe
srun -n 64 -m plane=52 output-${JOB_NAME}/prog.exe

