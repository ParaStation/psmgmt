#!/bin/bash

JOB_NAME=$(scontrol show job -o ${SLURM_JOB_ID} | python2 -c 'import sys ; import os; d = dict([(x[0], "=".join(x[1:])) for x in map(lambda u: u.split("="), sys.stdin.read().split())]) ; x = d["Name"] if "Name" in d.keys() else d["JobName"] ; print(x)')

# Juropa-3 specific :(
module load intel-para
mpicc prog.c -o output-${JOB_NAME}/prog.exe

export I_MPI_FABRICS=tcp
export I_MPI_FALLBACK=off

srun --mpi pmi2 -n 1 output-${JOB_NAME}/prog.exe

