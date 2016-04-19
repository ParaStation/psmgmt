#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

# Juropa-3 specific :(
module load iimpi
mpiicc prog.c -o output-${JOB_NAME}/prog.exe

export I_MPI_FABRICS=tcp
export I_MPI_FALLBACK=off

srun --mpi pmi2 -n 1 output-${JOB_NAME}/prog.exe 

