#!/usr/bin/env python
#
#               ParaStation psmom test suite
#
# Copyright (C) 2011 ParTec Cluster Competence Center GmbH, Munich
#
# author
# Michael Rauh (rauh@par-tec.com)
#

import os

class rth:

    def __init__(self):
        self.allEnv = {}

    def testEnv(self, key, value, ex=0):
        if value == None:
            value = ""

        if key not in self.allEnv:
            print("env variable '" + key + "' not found in output")
            exit(1)

        if ex == 1:
            if self.allEnv[key] == "":
                print("empty env variable '" + self.allEnv[key] + "'")
                exit(1)
            return

        if self.allEnv[key] != value:
            print("error: variable '" + key + "' has value '" + self.allEnv[key]
                    + "' should be '" + str(value) + "'")
            exit(1)


    def parseOutput(self, outfile):
        if os.path.isfile(str(outfile)) == False:
            print("open output file '" + str(outfile) + "' failed")
            exit(1)
        for line in open(outfile,'r').readlines():
            if line.find("=") == -1:
                continue
            env = (line.split("=", 1))
            self.allEnv[env[0]] = env[1][:env[1].find("\n")]
