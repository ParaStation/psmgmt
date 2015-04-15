#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

gcc prog1.c -o output-${JOB_NAME}/prog1.exe 
gcc prog2.c -o output-${JOB_NAME}/prog2.exe 

for SZ in 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768
do
	output-${JOB_NAME}/prog1.exe ${SZ} | srun -n 4 output-${JOB_NAME}/prog2.exe ${SZ} > output-${JOB_NAME}/${SZ}.txt 2>&1
done

