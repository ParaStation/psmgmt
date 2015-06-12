#!/bin/bash

export MALLOC_CHECK_=2

srun -n 1 ./test.py

