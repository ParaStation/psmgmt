#!/bin/bash

trap "echo SIGUSR1; exit" SIGUSR1

while true ; do true ; done

