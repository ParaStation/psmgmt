#!/bin/bash

srun -N 1 -n 1 true

X=$?
echo ${X}

srun -N 2 -n 2 --ntasks-per-node 1 true

X=$?
echo ${X}

exit ${X}

