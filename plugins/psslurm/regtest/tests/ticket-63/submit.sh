#!/bin/bash

ulimit -u

ulimit -u 4096
ulimit -u
srun -N 1 -n 1 --propagate=NPROC ./ulimit.sh

ulimit -u 2048
ulimit -u
srun -N 1 -n 1 --propagate=NPROC ./ulimit.sh 

