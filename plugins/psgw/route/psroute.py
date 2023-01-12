#!/usr/bin/env python3

# pylint: disable=invalid-name
# pylint: disable=missing-docstring

import os
import sys
import traceback
import re

from pypsconfig import PSConfig

sys.path.append("%s/../psmgmt/" % os.path.dirname(__file__))
from pscompress import compress
from psexpand import expand_list

VERBOSE = 0

def vlog(msg):
    global VERBOSE
    if VERBOSE == 1:
        print(msg)

def parseEnv():
    global VERBOSE
    verbose = os.getenv("PSGW_VERBOSE")
    if verbose is not None:
        VERBOSE = 1

    uid = os.getenv("SLURM_JOB_UID")
    if uid is None or uid == "":
        raise Exception("Missing SLURM_JOB_UID")

    gid = os.getenv("SLURM_JOB_GID")
    if gid is None or gid == "":
        raise Exception("Missing SLURM_JOB_GID")

    user = os.getenv("SLURM_JOB_USER")
    if user is None or user == "":
        raise Exception("Missing SLURM_JOB_USER")

    jobid = os.getenv("SLURM_JOB_ID")
    if jobid is None or jobid == "":
        raise Exception("Missing SLURM_JOB_ID")

    plugin = os.getenv("SLURM_SPANK_PSGW_PLUGIN")
    if plugin is None or plugin == "":
        raise Exception("Missing SLURM_SPANK_PSGW_PLUGIN")

    rFile = os.getenv("_PSSLURM_ENV_PSP_GW_SERVER")
    if rFile is None or rFile == "":
        raise Exception("Missing _PSSLURM_ENV_PSP_GW_SERVER")

    home = os.getenv("HOME")
    if home is None or home == "":
        raise Exception("Missing HOME")

    nodeList = os.getenv("SLURM_PACK_JOB_NODELIST")
    if nodeList is None or nodeList == "":
        raise Exception("Missing SLURM_PACK_JOB_NODELIST")

    vlog("job: %s, user: %s, plugin: %s, "
         "route file: %s, nodeList: %s, home: %s" %
         (jobid, user, plugin, rFile, nodeList, home))

    return jobid, int(uid), int(gid), user, rFile, plugin, nodeList, home

def changeUser(uid, gid, user, home):
    # remove group memberships
    os.setgroups([])

    # set supplementary groups
    os.initgroups(user, gid)

    # change the uid/gid
    os.setgid(gid)
    os.setuid(uid)

    # change to home directory
    os.chdir(home)

def writeRouteFile(plugin, rFile, psgwdToPort, nodesA, nodesB):
    nodesA = sorted(expand_list(nodesA))
    nodesB = sorted(expand_list(nodesB))
    gateways = sorted(psgwdToPort.keys())

    if not gateways:
        raise Exception("No gateways available")

    routes = retrieveRoutes(plugin, nodesA, nodesB, gateways)

    gwIndex = {}
    for gw in gateways:
        gwIndex[gw] = 0

    count = 0
    with open(rFile, "w") as f:
        for gw in gateways:
            for nodeA, nodeB in routes[gw]:
                f.write("%s:%d %s %s\n" % (gw, psgwdToPort[gw][gwIndex[gw]],
                                           nodeA, nodeB))
                gwIndex[gw] += 1
                if gwIndex[gw] == len(psgwdToPort[gw]):
                    gwIndex[gw] = 0
                count += 1

    vlog("wrote %d lines to route file %s" % (count, rFile))

    assert count == len(nodesA)*len(nodesB)

def retrieveRoutes(plugin, nodesA, nodesB, gateways):
    pluginPath, pluginFile = os.path.split(plugin)
    sys.path.append(pluginPath)
    plu = __import__(re.sub(r'\.py$', r'', pluginFile))

    routes = {}

    for gw in gateways:
        routes[gw] = []

    if "routeConnectionX" in dir(plu) and plu.routeConnectionX is not None:
        for nodeA in nodesA:
            for nodeB in nodesB:
                err, gw = plu.routeConnectionX(nodesA, nodesB, gateways, nodeA, nodeB)
                if err is not None:
                    raise Exception("Failure in routeConnectionX")
                routes[gw] += [(nodeA, nodeB)]
    elif "routeConnectionS" in dir(plu) and plu.routeConnectionS is not None:
        for i, nodeA in enumerate(nodesA):
            for j, nodeB in enumerate(nodesB):
                err, gwId = plu.routeConnectionS(len(nodesA), len(nodesB), len(gateways), i, j)
                if err is not None:
                    raise Exception("Failure in routeConnectionS")
                routes[gateways[gwId]] += [(nodeA, nodeB)]
    else:
        raise Exception("Neither routeConnectionX() nor routeConnectionS() defined")

    return routes

def splitNodes(nodeList):
    config = PSConfig()
    cluster = []
    booster = []

    for node in expand_list(nodeList):
        IP = config.get("host:%s" % node, '%s.DevIPAddress' % \
                        (config.get("host:%s" % node, 'Psid.NetworkName')))
        data = config.getList("host:%s" % node, "Psid.HardwareTypes",
                              inherit=True, follow=True)
        if any("booster" in s for s in data):
            booster.append(IP)
        else:
            cluster.append(IP)

    nodesA = compress(cluster)
    vlog("Cluster nodes = %s" % nodesA)
    if not nodesA:
        raise Exception("No cluster nodes found")

    nodesB = compress(booster)
    vlog("Booster nodes = %s" % nodesB)
    if not nodesB:
        raise Exception("No booster nodes found")

    return nodesA, nodesB

def extractGateways():
    psgwdToPort = {}

    num = os.getenv("NUM_GATEWAYS")
    if num is None or num == "":
        raise Exception("Missing NUM_GATEWAYS")
    num = int(num)

    for x in range(num):
        gw = os.getenv("GATEWAY_ADDR_%d" % x)
        if gw is None or gw == "":
            raise Exception("Missing GATEWAY_ADDR_%d" % x)
        vlog("gw%d: %s" % (x, gw))
        if gw.split(":")[0] not in psgwdToPort:
            psgwdToPort[gw.split(":")[0]] = []
        psgwdToPort[gw.split(":")[0]].append(int(gw.split(":")[1]))

    return psgwdToPort

def handleException(reason, err):
    print("%s: %s" % (reason, err))
    traceback.print_exc()
    sys.exit(1)

def main():
    # Parse environment
    try:
        _, uid, gid, user, rFile, plugin, nodeList, home = parseEnv()
    except Exception as err:
        handleException("Parsing environment failed", err)

    # Continue execution as job owner
    try:
        changeUser(uid, gid, user, home)
    except Exception as err:
        handleException("Changing user failed", err)

    # Split cluster and booster nodes
    try:
        nodesA, nodesB = splitNodes(nodeList)
    except Exception as err:
        handleException("Splitting nodes %s failed" % nodeList, err)

    # Extract gateway nodes
    try:
        psgwdToPort = extractGateways()
    except Exception as err:
        handleException("Extracting gateways failed", err)

    # Write routing file
    try:
        writeRouteFile(plugin, rFile, psgwdToPort, nodesA, nodesB)
    except Exception as err:
        handleException("Writing routing file failed", err)

    vlog("success")

if __name__ == "__main__":
    main()
