#!/bin/bash

srun -N 1 -n 1 python hostname-loop.py&
srun -N 1 -n 1 python hostname-loop.py&
srun -N 1 -n 1 python hostname-loop.py&
srun -N 1 -n 1 python hostname-loop.py&

wait

