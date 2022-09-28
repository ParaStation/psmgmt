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


#include "pshostlist.h"

#include "pluginmalloc.h"

#include "psslurmlog.h"

/** List of all GRES configurations */
static LIST_HEAD(topoConfList);

static void freeTopologyConf(Topology_Conf_t *topo)
{
    ufree(topo->switchname);
    ufree(topo->switches);
    ufree(topo->nodes);
    ufree(topo->linkspeed);
    ufree(topo);
}

Topology_Conf_t *saveTopologyConf(Topology_Conf_t *topo)
{
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
    list_t *g, *tmp;
    list_for_each_safe(g, tmp, &topoConfList) {
	Topology_Conf_t *topo = list_entry(g, Topology_Conf_t, next);

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
    if (cInfo->res) return true;
    if (!strcmp(name, cInfo->name)) {
	cInfo->res = true;
    }
    return true;
}

static bool isChild(const char array[], const char* name)
{
    compInfo_t cInfo = {
	.name = name,
	.res = false
    };

    if (!array) return false;

    traverseHostList(array, compareName, &cInfo);

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
    size_t newsize = strlen(old) + 1 + strlen(sw) + 1;
    topo->address = umalloc(newsize);
    snprintf(topo->address, newsize, "%s.%s", sw, old);
    ufree(old);

    old = topo->pattern;
    newsize = strlen(old) + 7 + 1;
    topo->pattern = umalloc(newsize);
    snprintf(topo->pattern, newsize, "switch.%s", old);
    ufree(old);

    fdbg(PSSLURM_LOG_TOPO, "topology address '%s' ('%s')\n", topo->address,
	 topo->pattern);
}

Topology_t *getTopology(const char *node)
{
    Topology_t *topology = ucalloc(sizeof(topology));

    addNode(topology, node);

    /* looking for switch this node is directly connected to */
    list_t *g;
    Topology_Conf_t *topoconf;
    bool found = false;
    list_for_each(g, &topoConfList) {
	topoconf = list_entry(g, Topology_Conf_t, next);
	if (isChild(topoconf->nodes, node)) {
	    found = true;
	    break;
	}
    }

    if (!found) {
	/* no entry in topology.conf, assume node as standalone */
	fdbg(PSSLURM_LOG_TOPO, "node '%s' not found in config\n", node);
	return topology;
    }

    do {
	addSwitch(topology, topoconf->switchname);

	/* looking for switch current switch is child of */
	Topology_Conf_t *topoconf2;
	found = false;
	list_for_each(g, &topoConfList) {
	    topoconf2 = list_entry(g, Topology_Conf_t, next);
	    if (isChild(topoconf2->switches, topoconf->switchname)) {
		topoconf = topoconf2;
		found = true;
		break;
	    }
	}
    } while(found);

    return topology;
}
