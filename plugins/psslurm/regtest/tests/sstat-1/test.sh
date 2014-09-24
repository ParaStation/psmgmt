#!/bin/bash

srun -n 1 python ./loop.py &
srun -n 1 python ./loop.py

