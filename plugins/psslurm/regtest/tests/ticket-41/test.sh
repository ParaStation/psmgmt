#!/bin/bash

srun -N 1 -n 1 --ntasks-per-node=2 python hostname-loop.py&
srun -N 1 -n 1 --ntasks-per-node=2 python hostname-loop.py&
srun -N 1 -n 1 --ntasks-per-node=2 python hostname-loop.py&
srun -N 1 -n 1 --ntasks-per-node=2 python hostname-loop.py&

wait

