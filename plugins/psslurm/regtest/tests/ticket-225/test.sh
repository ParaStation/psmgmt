#!/bin/bash

srun --exclusive -N 1 -n 2 -c 8 true &
srun --exclusive -N 1 -n 2 -c 8 true &
srun --exclusive -N 1 -n 2 -c 8 true &
srun --exclusive -N 1 -n 2 -c 8 true &
srun --exclusive -N 1 -n 2 -c 8 true &
srun --exclusive -N 1 -n 2 -c 8 true &
srun --exclusive -N 1 -n 2 -c 8 true &
srun --exclusive -N 1 -n 2 -c 8 true &

wait

# Important: explicit return value
exit $?

