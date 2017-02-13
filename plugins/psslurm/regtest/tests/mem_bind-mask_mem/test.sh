#!/bin/bash

JOB_NAME=$(scontrol show job -o ${SLURM_JOB_ID} | python2 -c 'import sys ; import os; d = dict([(x[0], "=".join(x[1:])) for x in map(lambda u: u.split("="), sys.stdin.read().split())]) ; x = d["Name"] if "Name" in d.keys() else d["JobName"] ; print(x)')

env PATH=/usr/local/bin:/bin:/usr/bin:/usr/local/sbin:/usr/sbin:/sbin /usr/bin/gcc -DMEM_MASK=1 prog.c -o output-${JOB_NAME}/prog.exe -lnuma
srun -n 2 --mem_bind=mask_mem:0x2,0x3 --cpu_bind=socket output-${JOB_NAME}/prog.exe

