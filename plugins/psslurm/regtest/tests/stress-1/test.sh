#!/bin/bash

for i in {1..200} ; do (srun -N 1 -n 1 true ; echo $?) & done
wait

