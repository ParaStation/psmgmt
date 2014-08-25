#!/bin/bash

ulimit -s
srun -n 1 --propagate=STACK  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

ulimit -s 5120

ulimit -s
srun -n 1 --propagate=STACK  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

