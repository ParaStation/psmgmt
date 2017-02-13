#!/bin/bash

ulimit -c
srun -n 1 --propagate=CORE  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

ulimit -c 134217728

ulimit -c
srun -n 1 --propagate=CORE  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

