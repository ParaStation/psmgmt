#!/bin/bash

ulimit -m
srun -n 1 --propagate=RSS  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

ulimit -m 131072

ulimit -m
srun -n 1 --propagate=RSS  ./ulimit.sh
srun -n 1 --propagate=ALL ./ulimit.sh

