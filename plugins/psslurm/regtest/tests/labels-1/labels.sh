#!/bin/bash

# TODO	The 32 is machine dependent
srun -n 32 -l hostname

# Important: explicit return value
exit $?

