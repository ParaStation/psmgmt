#!/bin/bash

ulimit -n
srun -n 1 --propagate=NOFILE  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

ulimit -n 512

ulimit -n
srun -n 1 --propagate=NOFILE  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

