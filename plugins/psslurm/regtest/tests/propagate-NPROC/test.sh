#!/bin/bash

ulimit -u
srun -n 1 --propagate=NPROC  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

ulimit -u 512

ulimit -u
srun -n 1 --propagate=NPROC  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

