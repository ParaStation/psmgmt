#!/bin/bash

umask 023
export SLURM_UMASK=0023
umask

srun -n 1 /bin/bash -c umask

# Important: explicit return value
exit $?

