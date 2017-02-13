#!/bin/bash

./test.py
srun -n 1 -o output-${SLURM_JOB_NAME}/slurm-%8A-z-%4a.out -e output-${SLURM_JOB_NAME}/slurm-%8A-z-%4a.err ./test.py

