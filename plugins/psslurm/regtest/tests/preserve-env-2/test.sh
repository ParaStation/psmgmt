#!/bin/bash

SLURM_NNODES=5 srun -N 1 -n 1 -E ./slurm-env.sh

