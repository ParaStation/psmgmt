# ParaStation psslurm test suite

This document describes the `psslurm` test suite. 

## Overview

The `psslurm` test suite exercises the Slurm batch system with a variety of 
different tests. The test suite consists of a driver in `bin/regtest.py` and 
a number of tests located in the `tests` folder. To support writing 
evaluation routines in Python, a `helper` Python library is located in the
`lib` folder. Some of the tests are automatically generated. The 
corresponding scripts are located in the `scripts` folder in the root folder.

## Prerequisites

The test suite assumes that 

- A python installation is available (tested with 2.6.6)
- Slurm is installed in the default position

Some tests reference the `expect` scripts from the Slurm test suite. These
tests assume that the `expect` folder from the Slurm source code is placed
in the root directory of this test suite. Alternatively, the environment variable
`SLURM_TESTSUTE` can be set to point to the `expect` folder.
Use `upstream/download.py` to download the `expect` folder and apply 
fixes from `upstream/patches`.

## Running the test suite

To run the test suite execute `python ./bin/regtest.py` in the root directory.
The test suite driver supports several options (see the `-h` output for more
details):

- The `-t` flag can be used to specify a comma-separated list of tests to 
  execute.
- The `-m` flag can be used to specify a regular expression which is used to
  select those tests (among all available tests) that are executed.
- The `-i` flag allows for ignoring individual partitions.
- The `-r` flag can be used to requested multiple repetitions of each test.

## Writing tests

Each test is represented by a directory in the `tests` folder. Within the
directory a description file `descr.json` must be created. `descr.json`
specifies the `type` of the test (`batch` or `interactive`), the
`partitions` and `reservations` to test, the optional `submit` command,
the optional `fproc` frontend process command that runs in parallel to (or as
a replacement of) the submit command and well as the `eval` evaluation routine.

`regetest.py` creates an individualized output directory for each tests and
executes tests in parallel. This needs to be taken into account when writing
tests.

When writing `eval` routines in Python one can make use of the `helper`
library.

