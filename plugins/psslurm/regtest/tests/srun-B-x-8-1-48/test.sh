#!/bin/bash

JOB_NAME=$(scontrol show job -o ${SLURM_JOB_ID} | python2 -c 'import sys ; import os; d = dict([(x[0], "=".join(x[1:])) for x in map(lambda u: u.split("="), sys.stdin.read().split())]) ; x = d["Name"] if "Name" in d.keys() else d["JobName"] ; print(x)')

env PATH=/usr/local/bin:/bin:/usr/bin:/usr/local/sbin:/usr/sbin:/sbin /usr/bin/gcc prog.c -o output-${JOB_NAME}/prog.exe
srun -B \*:8:1 -n 48 output-${JOB_NAME}/prog.exe

