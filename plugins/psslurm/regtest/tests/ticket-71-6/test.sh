#!/bin/bash

export OMP_NUM_THREADS=32
srun -n 2 hostname

