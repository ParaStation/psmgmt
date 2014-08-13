#!/bin/bash

export OMP_NUM_THREADS=16
srun -n 4 hostname

