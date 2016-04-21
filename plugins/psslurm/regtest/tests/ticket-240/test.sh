#!/bin/bash

srun -n 2 true

srun -n 1 true &
srun -n 1 true &

wait

# Important: explicit return value
exit $?

