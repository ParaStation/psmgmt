#!/bin/bash

ulimit -f
srun -n 1 --propagate=FSIZE  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

ulimit -f 134217728

ulimit -f
srun -n 1 --propagate=FSIZE  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

