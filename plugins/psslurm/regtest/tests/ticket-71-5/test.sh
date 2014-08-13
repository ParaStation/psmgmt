#!/bin/bash

export OMP_NUM_THREADS=1024
srun -n 1 hostname

