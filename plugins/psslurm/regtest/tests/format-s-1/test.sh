#!/bin/bash

JOB_NAME=$(scontrol show job -o ${SLURM_JOB_ID} | python2 -c 'import sys ; import os; d = dict([(x[0], "=".join(x[1:])) for x in map(lambda u: u.split("="), sys.stdin.read().split())]) ; x = d["Name"] if "Name" in d.keys() else d["JobName"] ; print(x)')

for i in {0..7} ; do
	srun -N 4 -n 8 --ntasks-per-node=2 -o output-${JOB_NAME}/slurm-%j-%s.out -e output-${JOB_NAME}/slurm-%j-%s.err ./hostname.py ${i}
done

exit 0

