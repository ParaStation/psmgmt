#!/bin/bash

srun -N 2-4 -n 4 hostname

# Important: explicit return value
exit $?

