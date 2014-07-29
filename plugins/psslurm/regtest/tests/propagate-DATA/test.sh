#!/bin/bash

ulimit -d
srun -n 1 --propagate=DATA  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

ulimit -d 134217728

ulimit -d
srun -n 1 --propagate=DATA  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

