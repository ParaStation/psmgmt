#!/bin/bash

for i in {1..32}; do
	srun     --exclusive -n1 sleep 10 &
done

sleep 2

srun --immediate --exclusive -n1 hostname &

wait

