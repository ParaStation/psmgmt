#!/bin/bash

for i in {1..64} ; do
	srun -N 1 -n 1 -r $(($i%2)) hostname&
done

wait

