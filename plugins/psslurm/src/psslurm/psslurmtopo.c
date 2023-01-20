/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmtopo.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "pscommon.h"
#include "pshostlist.h"

#include "pluginmalloc.h"

#include "psslurmlog.h"

/** List of all topology configurations */
static LIST_HEAD(topoConfList);

static void freeTopologyConf(Topology_Conf_t *topo)
{
    if (!topo) return;
    ufree(topo->switchname);
    ufree(topo->switches);
    ufree(topo->nodes);
    ufree(topo->linkspeed);
    ufree(topo);
}

Topology_Conf_t *saveTopologyConf(Topology_Conf_t *topo)
{
    if (!topo) return NULL;
    flog("%s %s%s%s%s%s%s\n", topo->switchname,
	 topo->switches ? " switches=" : "",
	 topo->switches ? topo->switches : "",
	 topo->nodes ? " nodes=" : "",
	 topo->nodes ? topo->nodes : "",
	 topo->linkspeed ? " linkspeed=" : "",
	 topo->linkspeed ? topo->linkspeed : "");

    list_add_tail(&topo->next, &topoConfList);
    return topo;
}

void clearTopologyConf(void)
{
    list_t *t, *tmp;
    list_for_each_safe(t, tmp, &topoConfList) {
	Topology_Conf_t *topo = list_entry(t, Topology_Conf_t, next);

	list_del(&topo->next);
	freeTopologyConf(topo);
    }
}

typedef struct {
    const char *name;
    bool res;
} compInfo_t;

static bool compareName(char *name, void *info)
{
    compInfo_t *cInfo = info;
    if (!cInfo->res && !strcmp(name, cInfo->name)) cInfo->res = true;

    return true;
}

static bool isChild(const char nameList[], const char* name)
{
    if (!nameList) return false;

    compInfo_t cInfo = { .name = name, .res = false };
    traverseHostList(nameList, compareName, &cInfo);

    return cInfo.res;
}

static void addNode(Topology_t *topo, const char *node)
{
    if (topo->address || topo->pattern) {
	flog("topology not empty\n");
	return;
    }

    topo->address = ustrdup(node);
    topo->pattern = ustrdup("node");

    fdbg(PSSLURM_LOG_TOPO, "topology address '%s' ('%s')\n", topo->address,
	 topo->pattern);
}

static void addSwitch(Topology_t *topo, const char *sw)
{
    if (!topo->address || !topo->pattern) {
	flog("topology empty\n");
	return;
    }

    char *old = topo->address;
    topo->address = PSC_concat(sw, ".", old);
    ufree(old);

    old = topo->pattern;
    topo->pattern = PSC_concat("switch.", old);
    ufree(old);

    fdbg(PSSLURM_LOG_TOPO, "topology address '%s' ('%s')\n", topo->address,
	 topo->pattern);
}

Topology_t *getTopology(const char *node)
{
    Topology_t *topology = ucalloc(sizeof(topology));

    addNode(topology, node);

    /* looking for switch this node is directly connected to */
    Topology_Conf_t *switchConf = NULL;

    list_t *t;
    list_for_each(t, &topoConfList) {
	Topology_Conf_t *thisConf = list_entry(t, Topology_Conf_t, next);
	if (isChild(thisConf->nodes, node)) {
	    switchConf = thisConf;
	    break;
	}
    }

    if (!switchConf) {
	/* no entry in topology.conf, assume node as standalone */
	fdbg(PSSLURM_LOG_TOPO, "node '%s' not found in config\n", node);
	return topology;
    }

    while (switchConf) {
	addSwitch(topology, switchConf->switchname);
	char *thisName = switchConf->switchname;

	/* looking for switch current switch is child of */
	switchConf = NULL;
	list_for_each(t, &topoConfList) {
	    Topology_Conf_t *thisConf = list_entry(t, Topology_Conf_t, next);
	    if (isChild(thisConf->switches, thisName)) {
		switchConf = thisConf;
		break;
	    }
	}
    }

    return topology;
}

void clearTopology(Topology_t *topo)
{
    if (!topo) return;

    ufree(topo->address);
    ufree(topo->pattern);
    ufree(topo);
}
