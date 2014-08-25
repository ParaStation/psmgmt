#!/bin/bash

./test.py
srun -n 1 -o output-${SLURM_JOB_NAME}/slurm-%A-z-%a.out -e output-${SLURM_JOB_NAME}/slurm-%A-z-%a.err ./test.py

