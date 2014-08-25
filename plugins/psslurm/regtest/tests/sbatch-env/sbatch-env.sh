#!/bin/bash

env | grep SBATCH

# Important: explicit return value
exit $?

