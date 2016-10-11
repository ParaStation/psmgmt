#!/bin/bash

JOB_NAME=$(scontrol show job -o ${SLURM_JOB_ID} | python2 -c 'import sys ; import os; d = dict([(x[0], "=".join(x[1:])) for x in map(lambda u: u.split("="), sys.stdin.read().split())]) ; x = d["Name"] if "Name" in d.keys() else d["JobName"] ; print(x)')

env PATH=/usr/local/bin:/bin:/usr/bin:/usr/local/sbin:/usr/sbin:/sbin /usr/bin/gcc prog1.c -o output-${JOB_NAME}/prog1.exe
env PATH=/usr/local/bin:/bin:/usr/bin:/usr/local/sbin:/usr/sbin:/sbin /usr/bin/gcc prog2.c -o output-${JOB_NAME}/prog2.exe

for SZ in 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768
do
	output-${JOB_NAME}/prog1.exe ${SZ} | srun -n 4 output-${JOB_NAME}/prog2.exe ${SZ} > output-${JOB_NAME}/${SZ}.txt 2>&1
done

