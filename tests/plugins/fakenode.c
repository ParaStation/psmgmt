/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "pscommon.h"
#include "pluginlog.h"
#include "pluginmalloc.h"

#include "nodeinfohandles.h"

#include "psidnodes.h"
#include "psidplugin.h"

#include "plugin.h"

int requiredAPI = 129;

char name[] = "fakenode";

int version = 100;

plugin_dep_t dependencies[] = {
    { "nodeinfo", 2 },
    { NULL, 0 } };

/** value of HWLOC_XMLFILE environment found at startup */
static char *oldHWLOC_XMLFILE = NULL;

/** value od PSIDnodes' pinProc found at startup */
static int oldPinProcs = 0;

/** value od PSIDnodes' bindMem found at startup */
static int oldBindMem = 0;

/** Flag to mark initializiation */
static bool initialized = false;

/** Name of the fake topology currently loaded */
static char *curTopology = NULL;

int initialize(void)
{
    initPluginLogger(name, NULL);

    void *nodeInfoHandle = PSIDplugin_getHandle("nodeinfo");
    if (!nodeInfoHandle) {
	pluginlog("%s: getting nodeinfo handle failed\n", __func__);
	return 1;
    }

    reinitNodeInfo = dlsym(nodeInfoHandle, "reinitNodeInfo");
    if (!reinitNodeInfo) {
	pluginlog("%s: getting reinitNodeInfo() function failed\n", __func__);
	return 1;
    }

    char *env = getenv("HWLOC_XMLFILE");
    if (env) oldHWLOC_XMLFILE = strdup(env);
    oldPinProcs = PSIDnodes_pinProcs(PSC_getMyID());
    oldBindMem = PSIDnodes_bindMem(PSC_getMyID());

    initialized = true;

    pluginlog("%s: (%i) successfully started\n", name, version);

    return 0;
}

static void resetFakeTopology(void)
{
    if (oldHWLOC_XMLFILE) {
	setenv("HWLOC_XMLFILE", oldHWLOC_XMLFILE, 1);
    } else {
	unsetenv("HWLOC_XMLFILE");
    }
    reinitNodeInfo();
    PSIDnodes_setPinProcs(PSC_getMyID(), oldPinProcs);
    PSIDnodes_setBindMem(PSC_getMyID(), oldBindMem);
}

void cleanup(void)
{
    pluginlog("%s\n", __func__);

    if (initialized) resetFakeTopology();
    free(oldHWLOC_XMLFILE);
    free(curTopology);

    pluginlog("%s: Done\n", __func__);
}

char * help(void)
{
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };

    addStrBuf("\nTest plugin helping to fake hwloc topologies.\n", &strBuf);
    addStrBuf("Topologies might be set via psiadmin:\n", &strBuf);
    addStrBuf("  plugin set ", &strBuf);
    addStrBuf(name, &strBuf);
    addStrBuf(" topology <name>\n", &strBuf);
    addStrBuf("<name> addresses to the hwloc topology file to be used\n",
	      &strBuf);
    addStrBuf("The file has to be available as\n", &strBuf);
    addStrBuf("  /opt/parastation/plugins/hwloc/topo.<name>.xml\n", &strBuf);
    addStrBuf("As an alternative the topology file might be addresses by"
	      "an absolute path\n", &strBuf);

    return strBuf.buf;
}

char *set(char *key, char *value)
{
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };

    if (!strcmp(key, "topology")) {
	if (!value) {
	    addStrBuf("  No value given", &strBuf);
	    return strBuf.buf;
	}
	char *topoFile;
	if (*value == '/') {
	    topoFile = strdup(value);
	} else {
	    char *installDir = PSC_lookupInstalldir(NULL);
	    topoFile = PSC_concat(installDir, "/plugins/hwloc/topo.",
				  value, ".xml", 0L);
	}
	struct stat fstat;
	//if (stat(topoFile, &fstat)) {
	int ret = stat(topoFile, &fstat);
	if (ret) {
	    pluginwarn(errno, "%s: stat(%s)", __func__, topoFile);
	    addStrBuf("  Topology file '", &strBuf);
	    addStrBuf(topoFile, &strBuf);
	    addStrBuf("' not found\n", &strBuf);
	    free(topoFile);
	    return strBuf.buf;
	}

	setenv("HWLOC_XMLFILE", topoFile, 1);
	free(topoFile);
	reinitNodeInfo();
	PSIDnodes_setPinProcs(PSC_getMyID(), 0);
	PSIDnodes_setBindMem(PSC_getMyID(), 0);
	free(curTopology);
	curTopology = strdup(value);

	return NULL;
    }

    addStrBuf("  Unknown key '", &strBuf);
    addStrBuf(key, &strBuf);
    addStrBuf("'\n", &strBuf);

    return strBuf.buf;
}

char *unset(char *key)
{
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };

    if (!strcmp(key, "topology") && curTopology) {
	resetFakeTopology();
	free(curTopology);
	curTopology = NULL;
	return NULL;
    }

    addStrBuf("  Unknown key '", &strBuf);
    addStrBuf(key, &strBuf);
    addStrBuf("'\n", &strBuf);

    return strBuf.buf;
}

char *show(char *key)
{
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };

    if (curTopology) {
	addStrBuf("  Current fake topologie taken from ", &strBuf);
	addStrBuf(curTopology, &strBuf);
	addStrBuf("\n", &strBuf);
    } else {
	addStrBuf("  Current topologie is real\n", &strBuf);
    }
    if (key) {
	addStrBuf("  Key '", &strBuf);
	addStrBuf(key, &strBuf);
	addStrBuf("' is ignored\n", &strBuf);
    }

    return strBuf.buf;
}
