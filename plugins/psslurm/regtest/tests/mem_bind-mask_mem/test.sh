#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

env PATH=/usr/local/bin:/bin:/usr/bin:/usr/local/sbin:/usr/sbin:/sbin /usr/bin/gcc -DMEM_MASK=1 prog.c -o output-${JOB_NAME}/prog.exe -lnuma
srun -n 2 --mem_bind=mask_mem:0x2,0x3 --cpu_bind=socket output-${JOB_NAME}/prog.exe

