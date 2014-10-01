#!/bin/bash

env | grep SLURM

# Important: explicit return value
exit $?

