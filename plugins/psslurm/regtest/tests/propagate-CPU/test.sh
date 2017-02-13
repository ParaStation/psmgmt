#!/bin/bash

ulimit -t
srun -n 1 --propagate=CPU  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

ulimit -t 600

ulimit -t
srun -n 1 --propagate=CPU  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

