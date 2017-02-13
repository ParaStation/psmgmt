#!/bin/bash

srun -N 1 -n 1      hostname
srun -N 1 -n 1 -r 0 hostname
srun -N 1 -n 1 -r 1 hostname

