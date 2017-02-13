#!/bin/bash

env | grep SLURM | grep ARRAY

# Important: explicit return value
exit $?

