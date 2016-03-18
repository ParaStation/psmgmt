#!/bin/bash

O=$(srun -N 1 -n 1 true 2>&1)

X=$?
echo ${X}

echo ${O} | grep -i error

O=$(srun -N 2 -n 2 --ntasks-per-node 1 true 2>&1)

X=$?
echo ${X}

echo ${O} | grep -i error

exit ${X}

