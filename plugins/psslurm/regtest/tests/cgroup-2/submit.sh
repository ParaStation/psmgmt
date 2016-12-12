#!/bin/bash

srun -n 1 /bin/cat /proc/self/cgroup

# Important: explicit return value
exit $?

