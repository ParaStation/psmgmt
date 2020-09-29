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
#include "pluginpsconfig.h"

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

/** Description of fakenode's configuration parameters */
static const pluginConfigDef_t confDef[] = {
    { "DebugMask", PLUGINCONFIG_VALUE_NUM,
      "Mask to steer debug output (ignored)" },
    { "Topology", PLUGINCONFIG_VALUE_STR, "Fake hwloc topology to load" },
    { NULL, PLUGINCONFIG_VALUE_NONE, NULL }
};

pluginConfig_t config = NULL;

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


static char * doEval(const char *key, const pluginConfigVal_t *val,
		     const void *info)
{
    StrBuffer_t strBuf = { .buf = NULL };

    if (!strcmp(key, "DebugMask")) {
	// uint32_t mask = val ? val->val.num : 0;
	// currently ignored
    } else if (!strcmp(key, "Topology")) {
	if (!val && curTopology) { // unset
	    resetFakeTopology();
	    free(curTopology);
	    curTopology = NULL;
	    return NULL;
	}
	char *valStr = val->val.str;
	if (!strlen(valStr)) {
	    addStrBuf("  No value given", &strBuf);
	    return strBuf.buf;
	}
	char *topoFile;
	if (*valStr == '/') {
	    topoFile = strdup(valStr);
	} else {
	    char *installDir = PSC_lookupInstalldir(NULL);
	    topoFile = PSC_concat(installDir, "/plugins/hwloc/topo.",
				  valStr, ".xml", 0L);
	}
	struct stat fstat;
	if (stat(topoFile, &fstat)) {
	    pluginwarn(errno, "%s: stat(%s)", __func__, topoFile);
	    addStrBuf("  Topology file '", &strBuf);
	    addStrBuf(topoFile, &strBuf);
	    addStrBuf("' not found\n", &strBuf);
	    pluginConfig_unset(config, key);
	    free(topoFile);
	    return strBuf.buf;
	}

	setenv("HWLOC_XMLFILE", topoFile, 1);
	free(topoFile);
	reinitNodeInfo();
	PSIDnodes_setPinProcs(PSC_getMyID(), 0);
	PSIDnodes_setBindMem(PSC_getMyID(), 0);
	free(curTopology);
	curTopology = strdup(valStr);

	return NULL;
    } else {
	addStrBuf("  Unknown key '", &strBuf);
	addStrBuf(key, &strBuf);
	addStrBuf("'\n", &strBuf);
    }

    return strBuf.buf;
}


static bool evalValue(const char *key, const pluginConfigVal_t *val,
		      const void *info)
{
    char *ret = doEval(key, val, info);
    free(ret);

    return true;
}

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

    /* init configuration (depends on psconfig) */
    pluginConfig_new(&config);
    pluginConfig_setDef(config, confDef);

    pluginConfig_load(config, "FakeNode");
    pluginConfig_verify(config);

    /* Activate configuration values */
    pluginConfig_traverse(config, evalValue, NULL);

    initialized = true;

    pluginlog("%s: (%i) successfully started\n", name, version);

    return 0;
}

void cleanup(void)
{
    pluginlog("%s\n", __func__);

    if (initialized) resetFakeTopology();
    free(oldHWLOC_XMLFILE);
    free(curTopology);

    pluginConfig_destroy(config);
    config = NULL;

    pluginlog("%s: Done\n", __func__);
}

char * help(void)
{
    StrBuffer_t strBuf = { .buf = NULL };

    addStrBuf("\nTest plugin helping to fake hwloc topologies.\n", &strBuf);
    addStrBuf("Topologies might be set via psiadmin:\n", &strBuf);
    addStrBuf("  plugin set ", &strBuf);
    addStrBuf(name, &strBuf);
    addStrBuf(" Topology <name>\n", &strBuf);
    addStrBuf("<name> addresses to the hwloc topology file to be used\n",
	      &strBuf);
    addStrBuf("The file has to be available as\n", &strBuf);
    addStrBuf("  /opt/parastation/plugins/hwloc/topo.<name>.xml\n", &strBuf);
    addStrBuf("As an alternative the topology file might be addresses by"
	      " an absolute path\n", &strBuf);
    addStrBuf("\n# configuration options #\n\n", &strBuf);

    pluginConfig_helpDesc(config, &strBuf);
    return strBuf.buf;
}

char *set(char *key, char *value)
{
    StrBuffer_t strBuf = { .buf = NULL };
    const pluginConfigDef_t *thisDef = pluginConfig_getDef(config, key);

    if (!thisDef) {
	addStrBuf("  Unknown key '", &strBuf);
	addStrBuf(key, &strBuf);
	addStrBuf("'\n", &strBuf);
	return strBuf.buf;
    }

    if (!pluginConfig_addStr(config, key, value)) {
	addStrBuf("  Illegal value '", &strBuf);
	addStrBuf(value, &strBuf);
	addStrBuf("'\n", &strBuf);
	return strBuf.buf;
    }

    return doEval(key, pluginConfig_get(config, key), NULL);
}

char *unset(char *key)
{
    pluginConfig_remove(config, key);
    return doEval(key, NULL, config);
}

char *show(char *key)
{
    StrBuffer_t strBuf = { .buf = NULL };

    if (key) {
	if (!pluginConfig_showKeyVal(config, key, &strBuf)) {
	    addStrBuf(" '", &strBuf);
	    addStrBuf(key, &strBuf);
	    addStrBuf("' is unknown\n", &strBuf);
	}
    } else {
	if (curTopology) {
	    addStrBuf("  Current fake topologie taken from ", &strBuf);
	    addStrBuf(curTopology, &strBuf);
	    addStrBuf("\n", &strBuf);
	} else {
	    addStrBuf("  Current topologie is real\n", &strBuf);
	}
    }

    return strBuf.buf;
}
