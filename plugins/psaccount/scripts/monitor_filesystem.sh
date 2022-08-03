#!/bin/bash
#
# The file-system monitor script is supposed to output one line with the format
#
# readBytes:num writeBytes:num numReads:num numWrites:num
#
# readBytes = number of bytes read
# writeBytes = number of bytes written
# numReads = number of read operations
# numWrites = number of write operations
#
# An optional environment variable FILESYSTEM_TYPE may specify which
# file-system to monitor.
#

echo readBytes:0 writeBytes:0 numReads:0 numWrites:0
