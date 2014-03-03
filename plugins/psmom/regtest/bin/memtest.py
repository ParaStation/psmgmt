#!/usr/bin/env python2
#
#               ParaStation psmom test suite
#
# Copyright (C) 2011 ParTec Cluster Competence Center GmbH, Munich
#
# author
# Michael Rauh (rauh@par-tec.com)
#

import os, re

f = open("/tmp/malloc.txt", "r")

mem = {}

def parseLine(line):

    line = line[0:line.find("\n")]
    
    # parse free calls
    if line.find("ufree") != -1:
        line = line[line.find("ufree"):]

        #print(line)
        m = re.match(r"(?P<name>\w+)( |\t)+(?P<file>\w+)( |\t)+(?P<line>\d+)"
                        r"( |\t)+(?P<addr>(\w+|\(|\))+)", line)

        #print(m.group("addr"))

        addr = m.group("addr")
        name = m.group("file")
        line = m.group("line")

        # skip null frees
        if addr == "(nil)":
            return

        if addr not in mem:
            print("invalid free for not malloced addr '" + addr + "' file '" + name + ":" + line + "'" );
            return
        else:
            del(mem[addr])

        return

    
    # parse malloc calls
    if line.find("umalloc") != -1:
        line = line[line.find("umalloc"):]
        
        #print(line)
        m = re.match(r"(?P<name>\w+)( |\t)+(?P<file>\w+)( |\t)+(?P<line>\d+)"
                        r"( |\t)+(?P<addr>(\w+))( |\t)+\((?P<size>\d+)\)", line)

        addr = m.group("addr")
        name = m.group("file")
        line = m.group("line")
        
        if addr in mem:
            print("invalid second malloc for addr " + addr + "' file '" + name + ":" + line + "'");

        mem[m.group("addr")] = m.group("file") + ":" + m.group("line") +  " " + m.group("size")
        return

    
    # parse realloc calls
    if line.find("urealloc") != -1:
        line = line[line.find("urealloc"):]

        #print(line)
        m = re.match(r"(?P<name>\w+)( |\t)+(?P<file>\w+)( |\t)+(?P<line>\d+)"
                        r"( |\t)+(?P<new_addr>(\w+))( |\t)+\((?P<size>\d+)\)"
                        r"( |\t)+(?P<old_addr>\w+)", line)

        #print(m.group("new_addr"))
        

        # check if old address was malloced
        old_addr = m.group("old_addr")
        name = m.group("file")
        line = m.group("line")

        if old_addr in mem:
            del(mem[old_addr])

        # save new addr 
        new_addr = m.group("new_addr")
        mem[new_addr] = m.group("file") + ":" + m.group("line") +  " " + m.group("size")

        return
    

for line in f:
    parseLine(line)
f.close()


for item in mem:
    print("no free for " + item + ": " + mem[item])
