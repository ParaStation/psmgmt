#!/bin/bash

srun -n 4 --ntasks-per-node 2 -t 1 hostname

