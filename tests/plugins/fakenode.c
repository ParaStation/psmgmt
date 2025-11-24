/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "pscommon.h"
#include "psstrbuf.h"

#include "plugin.h"
#include "pluginlog.h"
#include "pluginpsconfig.h"
#include "psidnodes.h"
#include "psidplugin.h"

#include "nodeinfohandles.h"

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


static char *doEval(const char *key, const pluginConfigVal_t *val,
		    const void *info)
{
    if (!strcmp(key, "DebugMask")) {
	// uint32_t mask = val ? val->val.num : 0;
	// currently ignored
    } else if (!strcmp(key, "Topology")) {
	if (!val) {
	    // unset
	    if (curTopology) resetFakeTopology();
	    free(curTopology);
	    curTopology = NULL;
	    return NULL;
	}
	char *valStr = val->val.str;
	if (!strlen(valStr)) {
	    return strdup("  No value given");
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
	    strbuf_t buf = strbufNew(NULL);
	    pluginwarn(errno, "%s: stat(%s)", __func__, topoFile);
	    strbufAdd(buf, "  Topology file '");
	    strbufAdd(buf, topoFile);
	    strbufAdd(buf, "' not found\n");
	    pluginConfig_unset(config, key);
	    free(topoFile);
	    return strbufSteal(buf);
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
	strbuf_t buf = strbufNew(NULL);
	strbufAdd(buf, "  Unknown key '");
	strbufAdd(buf, key);
	strbufAdd(buf, "'\n");

	return strbufSteal(buf);
    }
    return NULL;
}


static bool evalValue(const char *key, const pluginConfigVal_t *val,
		      const void *info)
{
    char *ret = doEval(key, val, info);
    free(ret);

    return true;
}

int initialize(FILE *logfile)
{
    initPluginLogger(name, logfile);

    void *nodeInfoHandle = PSIDplugin_getHandle("nodeinfo");
    if (!nodeInfoHandle) {
	pluginflog("cannot get nodeinfo handle\n");
	return 1;
    }

    reinitNodeInfo = dlsym(nodeInfoHandle, "reinitNodeInfo");
    if (!reinitNodeInfo) {
	pluginflog("cannot access reinitNodeInfo()\n");
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
    pluginflog("\n");

    if (initialized) resetFakeTopology();
    free(oldHWLOC_XMLFILE);
    free(curTopology);

    pluginConfig_destroy(config);
    config = NULL;

    pluginflog("Done\n");
}

char * help(char *key)
{
    strbuf_t buf = strbufNew(NULL);

    strbufAdd(buf, "\nTest plugin helping to fake hwloc topologies.\n");
    strbufAdd(buf, "Topologies might be set via psiadmin:\n");
    strbufAdd(buf, "  plugin set ");
    strbufAdd(buf, name);
    strbufAdd(buf, " Topology <name>\n");
    strbufAdd(buf, "<name> addresses to the hwloc topology file to be used\n");
    strbufAdd(buf, "The file has to be available as\n");
    strbufAdd(buf, "  /opt/parastation/plugins/hwloc/topo.<name>.xml\n");
    strbufAdd(buf, "As an alternative the topology file might be addresses by"
	      " an absolute path\n");
    strbufAdd(buf, "\n# configuration options #\n\n");

    pluginConfig_helpDesc(config, buf);

    return strbufSteal(buf);
}

char *set(char *key, char *value)
{
    const pluginConfigDef_t *thisDef = pluginConfig_getDef(config, key);

    if (!thisDef) {
	strbuf_t buf = strbufNew(NULL);
	strbufAdd(buf, "  Unknown key '");
	strbufAdd(buf, key);
	strbufAdd(buf, "'\n");
	return strbufSteal(buf);
    }

    if (!pluginConfig_addStr(config, key, value)) {
	strbuf_t buf = strbufNew(NULL);
	strbufAdd(buf, "  Illegal value '");
	strbufAdd(buf, value);
	strbufAdd(buf, "'\n");
	return strbufSteal(buf);
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
    strbuf_t buf = strbufNew(NULL);

    if (key) {
	if (!pluginConfig_showKeyVal(config, key, buf)) {
	    strbufAdd(buf, " '");
	    strbufAdd(buf, key);
	    strbufAdd(buf, "' is unknown\n");
	}
    } else if (curTopology) {
	strbufAdd(buf, "  Current fake topologie taken from ");
	strbufAdd(buf, curTopology);
	strbufAdd(buf, "\n");
    } else {
	strbufAdd(buf, "  Current topologie is real\n");
    }

    return strbufSteal(buf);
}
