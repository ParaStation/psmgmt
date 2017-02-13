#!/bin/bash

ulimit -l
srun -n 1 --propagate=MEMLOCK  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

ulimit -l 134217728

ulimit -l
srun -n 1 --propagate=MEMLOCK  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

