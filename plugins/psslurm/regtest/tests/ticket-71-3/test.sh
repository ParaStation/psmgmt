#!/bin/bash

export OMP_NUM_THREADS=32
srun -n 1 hostname

