#!/bin/bash

ulimit -v
srun -n 1 --propagate=AS  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

ulimit -v 134217728

ulimit -v
srun -n 1 --propagate=AS  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

