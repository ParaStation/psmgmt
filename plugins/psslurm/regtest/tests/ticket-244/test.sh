#!/bin/bash

srun -O --multi-prog mp.conf

# Important: explicit return value
exit $?

