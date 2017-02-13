#!/bin/bash

SLURM_NTASKS=5 srun -N 1 -n 1 -E ./slurm-env.sh

