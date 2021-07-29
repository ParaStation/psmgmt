/*
 * ParaStation
 *
 * Copyright (C) 2009-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/types.h>

#include "list.h"
#include "plugin.h"
#include "psprotocol.h"
#include "pspluginprotocol.h"
#include "pscommon.h"
#include "config_parsing.h"
#include "psidutil.h"
#include "psidcomm.h"
#include "psidtask.h"
#include "psidnodes.h"

#include "psidplugin.h"

typedef int initFunc_t(FILE *logfile);

typedef void voidFunc_t(void);

typedef char *helpFunc_t(void);

typedef char *setFunc_t(char *key, char *value);

typedef char *keyFunc_t(char *key);

/** Structure holding all information concerning a plugin */
typedef struct {
    list_t next;             /**< Used to put into @ref pluginList */
    list_t triggers;         /**< List of plugins triggering this one */
    list_t depends;          /**< List of plugins this one depends on */
    void *handle;            /**< Handle created by dlopen() */
    char *name;              /**< Actual name */
    initFunc_t *initialize;  /**< Initializer (after dependencies resolved) */
    voidFunc_t *finalize;    /**< Finalize (trigger plugin's stop) */
    voidFunc_t *cleanup;     /**< Cleanup (immediately before unload) */
    helpFunc_t *help;        /**< Some help message from the plugin */
    setFunc_t *set;          /**< Modify plugin's internal state */
    keyFunc_t *unset;        /**< Unset plugin's internal state */
    keyFunc_t *show;         /**< Show plugin's internal state */
    int version;             /**< Actual version */
    int distance;            /**< Distance from origin on force */
    bool cleared;            /**< Flag plugin ready to finalize on force */
    bool finalized;          /**< Flag call of plugin's finalize() method */
    bool unload;             /**< Flag plugin to become unloaded */
    struct timeval load;     /**< Time when plugin was loaded */
    struct timeval grace;    /**< Grace period before forcefully unload */
} plugin_t;

/**
 * Structure used to create @a trigger and @a depends members of @ref
 * plugin_t
 */
typedef struct {
    plugin_t * plugin; /**< Corresponding plugin added to the list */
    list_t next;       /**< Actual list entry */
} plugin_ref_t;

/** List of plugins currently loaded */
static LIST_HEAD(pluginList);

/**
 * API version currently implemented
 *
 * Changes so far:
 *
 * 101: first API implementation
 *
 * 102: added PSIDHOOK_NODE_UP, PSIDHOOK_NODE_DOWN,
 *      PSID_registerDropper(), and dynamical loggers.
 *
 * 103: added PSIDHOOK_CREATEPART
 *
 * 104: added PSIDHOOK_SHUTDOWN
 *
 * 105: added PSC_setProcTitle()
 *
 * 106: added Timer_registerEnhanced()
 *
 * 107: next gen API supports set()/unset()/show()/help() methods
 *
 * 108: added PSIDHOOK_MASTER_GETPART, PSIDHOOK_MASTER_FINJOB,
 *	PSIDHOOK_MASTER_RECPART, PSIDHOOK_MASTER_EXITPART
 *
 * 109: added PSIDHOOK_CREATEPARTNL
 *
 * 110: added PSIDHOOK_EXEC_FORWARDER, PSIDHOOK_EXEC_CLIENT,
 *	PSIDHOOK_FRWRD_INIT, PSIDHOOK_FRWRD_CINFO,
 *	PSIDHOOK_FRWRD_KVS, PSIDHOOK_FRWRD_EXIT,
 *	PSIDHOOK_FRWRD_CLNT_RLS
 *
 * 111: added PSID_cancelCB()
 *
 * 112: dynamic resource allocation via PSIDHOOK_XTND_PART_DYNAMIC,
 *      PSIDHOOK_RELS_PART_DYNAMIC and PSIDpart_extendRes()
 *
 * 113: added PSIDHOOK_EXEC_CLIENT_USER
 *
 * 114: added PSIDHOOK_PELOGUE_FINISH and PSIDHOOK_FRWRD_DSOCK
 *
 * 115: added PSIDHOOK_JAIL_CHILD
 *
 * 116: added PSIDHOOK_PELOGUE_PREPARE
 *
 * 117: added PSIDspawn_localTask()
 *
 * 118: added PSID_adjustLoginUID()
 *
 * 119: new hook PSIDHOOK_PELOGUE_START
 *
 * 120: new hook PSIDHOOK_RANDOM_DROP
 *
 * 121: new hook PSIDHOOK_PSSLURM_FINALLOC
 *
 * 122: new hook PSIDHOOK_PELOGUE_RES
 *
 * 123: new hook PSIDHOOK_FRWRD_CLNT_RES
 *
 * 124: PSIDHOOK_CLEARMEM gets aggressive flag as argument
 *
 * 125: new hook PSIDHOOK_GETRESERVATION
 *
 * 126: new hook PSIDHOOK_PELOGUE_OE
 *
 * 127: new hooks PSIDHOOK_PSSLURM_JOB_FWINIT, PSIDHOOK_PSSLURM_JOB_FWFIN,
 *	PSIDHOOK_PSSLURM_JOB_EXEC
 *
 * 128: new hook PSIDHOOK_PELOGUE_GLOBAL
 *
 * 129: new hook PSIDHOOK_DIST_INFO; new PSIDnode functionality
 *      _setNumNUMADoms(), _numNUMADoms(), _setCPUSet(), _CPUSet(),
 *      _setNumGPUs, numGPUs(), _setGPUSet(), _GPUSet(), _setNumNICs,
 *      numNICs(), _setNICSet(), _NICSet(),
 *
 * 130: new PSIDhw public functionality _getNumPCIDevs(), _getPCISets()
 *
 * 131: new hook PSIDHOOK_JAIL_TERM
 *
 * 132: new hook PSIDHOOK_EXEC_CLIENT_PREP
 */
static int pluginAPIVersion = 132;


/** Grace period between finalize and unload on forcefully unloads */
static int unloadTimeout = 4;

/** logfile to be used by all plugins (unless they decide otherwise) */
static FILE *pluginLogfile = NULL;

static int finalizePlugin(plugin_t * plugin);
static int unloadPlugin(plugin_t * plugin);

/** List of unused plugin-references */
static LIST_HEAD(refFreeList);

/** Chunk size for allocation new references via malloc() */
#define REF_CHUNK 128

/**
 * @brief Get new reference
 *
 * Provide a new and empty plugin-reference used to store references
 * to plugins in reference-lists. References are taken from @ref
 * refFreeList as long as it provides free references. If no reference
 * is available from @ref refFreeList, a new chunk of @ref REF_CHUNK
 * references is allocated via malloc().
 *
 * @return On success a pointer to the reference is returned. If
 * malloc() fails, NULL is returned.
 */
static plugin_ref_t * getRef(void){
    plugin_ref_t *ref;

    if (list_empty(&refFreeList)) {
	plugin_ref_t *refs = malloc(REF_CHUNK*sizeof(*refs));
	unsigned int i;

	if (!refs) return NULL;

	for (i=0; i<REF_CHUNK; i++) {
	    refs[i].plugin = NULL;
	    list_add_tail(&refs[i].next, &refFreeList);
	}
    }

    /* get list's first usable element */
    ref = list_entry(refFreeList.next, plugin_ref_t, next);
    list_del(&ref->next);
    INIT_LIST_HEAD(&ref->next);

    return ref;
}

/**
 * @brief Put reference
 *
 * Put a plugin-reference no longer used back into @ref
 * refFreeList. From here the reference might be re-used via @ref
 * getRef(). Plugin-references are used to store references to
 * plugins in reference-lists.
 *
 * @param ref The reference to be put back
 *
 * @return No return value.
 */
static void putRef(plugin_ref_t *ref) {
    ref->plugin = NULL;
    list_add_tail(&ref->next, &refFreeList);
}

/**
 * @brief Find reference
 *
 * Find the plugin-reference to the plugin @a plugin in the list of
 * references @a refList.
 *
 * @param refList The reference list to search in
 *
 * @param plugin The plugin to search for
 *
 * @return Return the reference to the plugin plugin, if it is found
 * within the list. Otherwise NULL is given back.
 */
static plugin_ref_t * findRef(list_t *refList, plugin_t *plugin)
{
    list_t *t;

    if (!refList || !plugin) return NULL;

    list_for_each(t, refList) {
	plugin_ref_t *ref = list_entry(t, plugin_ref_t, next);

	if (ref->plugin == plugin) return ref;
    }

    return NULL;
}

/**
 * @brief Add reference
 *
 * Add a reference to the plugin @a plugin to the list of
 * plugin-references @a refList.
 *
 * @param refList The list of plugin-references to act on
 *
 * @param plugin The plugin to add
 *
 * @return If the plugin-reference was added, 1 is returned. In case a
 * reference to @a plugin was already registered, 0 is given back. Or
 * -1 in case of an error.
 */
static int addRef(list_t *refList, plugin_t *plugin)
{
    plugin_ref_t *ref;

    if (findRef(refList, plugin)) return 0;

    ref = getRef();
    if (!ref) {
	PSID_warn(-1, errno, "%s", __func__);
	return -1;
    }

    ref->plugin = plugin;
    list_add_tail(&ref->next, refList);

    return 1;
}

/**
 * @brief Remove reference
 *
 * Remove the reference to the plugin @a plugin from the list of
 * plugin-references @a refList.
 *
 * @param refList The list of plugin-references to act on
 *
 * @param plugin The plugin to remove
 *
 * @return On success, the removed plugin is returned. Or NULL, if
 * the reference to @a plugin was not found in the list.
 */
static plugin_t * remRef(list_t *refList, plugin_t *plugin)
{
    plugin_ref_t *ref = findRef(refList, plugin);

    if (ref) {
	list_del(&ref->next);
	putRef(ref);

	return plugin;
    }

    return NULL;
}


/**
 * @brief Remove trigger
 *
 * Remove the triggering plugin @a trigger from the list of triggering
 * plugins of the plugin @a plugin.
 *
 * @param plugin The plugin the remove the trigger from
 *
 * @param trigger The triggering plugin to remove
 *
 * @return On success, the removed trigger is returned. Or NULL, if
 * the triggering plugin was not found in the list.
 */
static plugin_t * remTrigger(plugin_t *plugin, plugin_t *trigger)
{
    plugin_t *removed;

    if (!plugin || !trigger) return NULL;

    removed = remRef(&plugin->triggers, trigger);

    if (!removed) {
	PSID_log(plugin == trigger ? PSID_LOG_PLUGIN : -1,
		 "%s: trigger '%s' not found in '%s'\n",
		 __func__, trigger->name, plugin->name);
	return NULL;
    }

    if (list_empty(&plugin->triggers)) finalizePlugin(plugin);

    return removed;
}

/**
 * @brief Remove dependency
 *
 * Remove the depending plugin @a depend from the list of depending
 * plugins of the plugin @a plugin.
 *
 * @param plugin The plugin the remove the dependency from
 *
 * @param depend The depending plugin to remove
 *
 * @return On success, the removed dependency is returned. Or NULL, if
 * the depending plugin was not found in the list.
 */
static plugin_t * remDepend(plugin_t *plugin, plugin_t *depend)
{
    plugin_t *removed;

    if (!plugin || !depend) return NULL;

    removed = remRef(&plugin->depends, depend);
    if (!removed) {
	PSID_log(-1, "%s: dependency '%s' not found in '%s'\n",
		 __func__, depend->name, plugin->name);
	return NULL;
    }

    return removed;
}


/**
 * @brief Print list of plugin-references
 *
 * Create a string describing the list of plugin-references @a
 * pList. The character-string is written into @a buf. At most @a size
 * characters are written into buf.
 *
 * If @a pList would require more than @a size characters, the
 * describing string will be chopped.
 *
 * @param buf A buffer holding the created string upon return
 *
 * @param size Number of characters @a buf is able to hold
 *
 * @param refList The list of plugin-references to be displayed
 *
 * @return No return value.
 */
static void printRefList(char *buf, size_t size, list_t *refList)
{
    list_t *p;
    if (buf && size > 0) buf[0] = '\0';

    list_for_each(p, refList) {
	plugin_ref_t *ref = list_entry(p, plugin_ref_t, next);

	if (ref->plugin && ref->plugin->name
	    && &ref->plugin->triggers != refList)
	    snprintf(buf+strlen(buf), size-strlen(buf),
		     " %s", ref->plugin->name);
    }
}

/**
 * @brief Find plugin
 *
 * Find a plugin by its name @a pName from the list of plugins @ref
 * pluginList.
 *
 * @param pName Name of the plugin to find
 *
 * @return If the plugin was found, a pointer to the describing
 * structure is returned. Or NULL otherwise.
 */
static plugin_t * findPlugin(char *pName)
{
    list_t *p;

    if (!pName || ! *pName) return NULL;

    list_for_each(p, &pluginList) {
	plugin_t *plugin = list_entry(p, plugin_t, next);

	if (!strcmp(pName, plugin->name)) return plugin;
    }

    return NULL;
}

/**
 * @brief Create plugin structure
 *
 * Create and initialize a plugin structure. The structure will
 * describe the loaded plugin referred by @a handle with name @a pName
 * and version @a version.
 *
 * @param handle Handle of the plugin created via dlopen()
 *
 * @param pName Name of the plugin
 *
 * @param pVer Version of the plugin
 *
 * @return Return the newly created structure, or NULL, if some error
 * occurred.
 */
static plugin_t * newPlugin(void *handle, char *pName, int pVer)
{
    plugin_t *plugin;

    PSID_log(PSID_LOG_PLUGIN, "%s(%p, %s, %d)\n",__func__, handle, pName, pVer);

    plugin = malloc(sizeof(*plugin));
    if (!plugin) {
	PSID_warn(-1, errno, "%s", __func__);
	return NULL;
    }

    plugin->name = strdup(pName);
    plugin->handle = handle;
    plugin->version = pVer;
    INIT_LIST_HEAD(&plugin->next);
    INIT_LIST_HEAD(&plugin->triggers);
    INIT_LIST_HEAD(&plugin->depends);

    if (handle) {
	plugin->initialize = dlsym(handle, "initialize");
	plugin->finalize = dlsym(handle, "finalize");
	plugin->cleanup = dlsym(handle, "cleanup");
	plugin->help = dlsym(handle, "help");
	plugin->set = dlsym(handle, "set");
	plugin->unset = dlsym(handle, "unset");
	plugin->show = dlsym(handle, "show");
    }

    plugin->distance = 0;
    plugin->cleared = false;
    plugin->finalized = false;
    plugin->unload = false;
    timerclear(&plugin->grace);

    return plugin;
}

/**
 * @brief Delete plugin
 *
 * Delete the plugin @a plugin. After passing some consistency-tests
 * all the memory occupied by the describing structure are freed.
 *
 * The consistency-tests include testing the trigger- and
 * dependant-lists to be empty and the plugin-handle to be NULL.
 *
 * @param plugin The plugin to be deleted
 *
 * @return No return value.
 */
static void delPlugin(plugin_t *plugin)
{
    char line[80];

    if (!plugin) return;

    if (!list_empty(&plugin->triggers)) {
	printRefList(line, sizeof(line), &plugin->triggers);
	PSID_log(-1, "%s: '%s' still triggered by: %s\n",
		 __func__, plugin->name, line);
	return;
    }
    if (!list_empty(&plugin->depends)) {
	printRefList(line, sizeof(line), &plugin->depends);
	PSID_log(-1, "%s: '%s' still depends on: %s\n",
		 __func__, plugin->name, line);
	return;
    }
    if (plugin->name) free(plugin->name);
    if (plugin->handle)
	PSID_log(-1, "%s: handle %p still exists\n", __func__, plugin->handle);

    free(plugin);
}

/**
 * @brief Register plugin
 *
 * Register the plugin @a new to the list of currently loaded plugins
 * @ref pluginList.
 *
 * @param new The plugin to register
 *
 * @return If a plugin with the same name is already registered, a
 * pointer to this plugin is given back. If the new plugin @a new is
 * registered successfully, NULL is returned.
 */
static plugin_t * registerPlugin(plugin_t * new)
{
    plugin_t *plugin = findPlugin(new->name);

    PSID_log(PSID_LOG_PLUGIN, "%s: '%s' ver %d\n",
	     __func__, new->name, new->version);
    if (plugin) {
	PSID_log(-1, "%s: '%s' already registered with version %d\n",
		 __func__, plugin->name, plugin->version);
	return plugin;
    }

    list_add(&new->next, &pluginList);

    return NULL;
}

int PSIDplugin_getUnloadTmout(void)
{
    return unloadTimeout;
}

void PSIDplugin_setUnloadTmout(int tmout)
{
    if (tmout < 0) {
	PSID_log(-1, "%s: Illegal value for timeout: %d\n", __func__, tmout);
    } else {
	unloadTimeout = tmout;
    }
}

int PSIDplugin_getNum(void)
{
    list_t *p;
    int num = 0;

    list_for_each(p, &pluginList) {
	plugin_t *plugin = list_entry(p, plugin_t, next);

	if (!plugin->unload) num++;
    }

    return num;
}

/**
 * @brief Load plugin
 *
 * Load the plugin @a pName with minimum version @a minVer. If loading
 * the plugin is triggered by the requirements of another plugin, @a
 * trigger has to hold this plugin.
 *
 * Loading a plugin might fail for several reasons. Besides obvious
 * problem like non-existing plugins or problems within dlopen(), this
 * might also include version mismatch, etc.
 *
 * Two types of version-matches are tested. First of all the current
 * API-version of the loading daemon has to fulfill the plugin's
 * requirements. Furthermore the plugin's version has to fulfill the
 * requirements set by @a minVer. If any version of a plugin is okay,
 * @a minVer might be set to 0.
 *
 * Loading a plugin might also fail due to loading dependent plugins
 * without success.
 *
 * If @a trigger is different from NULL, the corresponding plugin will
 * be marked as a triggering plugin within the newly created plugin.
 *
 * @param pName Name of the plugin to load
 *
 * @param minVer Minimal required version of the plugin. Might be 0
 *
 * @param trigger Plugin triggering the current plugin to be loaded
 *
 * @param logfile Logging destination to be used by the plugin and all
 * dependent plugins unless they decide otherwise
 *
 * @return Upon success, i.e. if the plugin is loaded afterward, the
 * structure describing the plugin is given back. Or NULL, if the
 * plugin could not be loaded.
 */
static plugin_t * loadPlugin(char *pName, int minVer, plugin_t * trigger,
			     FILE *logfile)
{
    char filename[PATH_MAX], *instDir;
    void *handle = NULL;

    int *plugin_reqAPI = NULL;
    char *plugin_name = NULL;
    int *plugin_version = NULL;
    plugin_dep_t *plugin_deps = NULL;

    plugin_t *plugin = findPlugin(pName);

    if (!pName || ! *pName) {
	PSID_log(-1, "%s: No name given\n", __func__);
	return NULL;
    }

    PSID_log(PSID_LOG_PLUGIN, "%s(%s, %d, %s)\n", __func__,
	     pName, minVer, trigger ? trigger->name : "<no trigger>");

    if (plugin) {
	PSID_log(PSID_LOG_PLUGIN, "%s: version %d already loaded\n",
		 __func__, plugin->version);
	if (plugin->version < minVer) {
	    PSID_log(-1, "%s: version %d of '%s' too small. %d required\n",
		     __func__, plugin->version, plugin->name, minVer);
	    return NULL;
	}

	if (plugin->finalized) {
	    PSID_log(-1, "%s: plugin '%s' already finalized\n", __func__,
		     plugin->name);
	    return NULL;
	}

	if (addRef(&plugin->triggers, trigger ? trigger : plugin) < 0) {
	    return NULL;
	}

	return plugin;
    }

    instDir = getenv("PSID_PLUGIN_PATH");
    if (!instDir) instDir = PSC_lookupInstalldir(NULL);
    if (!instDir) {
	PSID_log(-1, "%s: installation directory not found\n", __func__);
	return NULL;
    }
    snprintf(filename, sizeof(filename), "%s/plugins/%s.so", instDir, pName);
    handle = dlopen(filename, RTLD_NOW);

    if (!handle) {
	PSID_log(-1, "%s: dlopen(%s) failed: %s\n", __func__, filename,
		 dlerror());
	return NULL;
    }

    plugin_reqAPI = dlsym(handle, "requiredAPI");
    plugin_name = dlsym(handle, "name");
    plugin_version = dlsym(handle, "version");
    plugin_deps = dlsym(handle, "dependencies");

    if (!plugin_reqAPI) {
	PSID_log(PSID_LOG_PLUGIN, "%s: any API accepted\n", __func__);
    } else {
	PSID_log(PSID_LOG_PLUGIN, "%s: API version %d or above required\n",
		 __func__, *plugin_reqAPI);
    }
    if (plugin_reqAPI && pluginAPIVersion < *plugin_reqAPI) {
	PSID_log(-1, "%s: '%s' needs API version %d or above. This is %d\n",
		 __func__, pName, *plugin_reqAPI, pluginAPIVersion);
	dlclose(handle);
	return NULL;
    }

    if (!plugin_name || !*plugin_name) {
	PSID_log(-1, "%s: plugin-file '%s' does not define name\n", __func__,
		 filename);
	dlclose(handle);
	return NULL;
    } else if (strcmp(plugin_name, pName)) {
	PSID_log(-1, "%s: WARNING: plugin_name '%s' and name '%s' differ\n",
		 __func__, plugin_name, pName);
    }

    if (!plugin_version) {
	PSID_log(-1, "%s: Cannot determine version of '%s'\n",
		 __func__, pName);
	dlclose(handle);
	return NULL;
    }

    PSID_log(PSID_LOG_PLUGIN, "%s: plugin_version is %d\n", __func__,
	     *plugin_version);

    if (minVer && *plugin_version < minVer) {
	PSID_log(-1, "%s: 'version %d or above of '%s' required. This is %d\n",
		 __func__, minVer, pName, *plugin_version);
	dlclose(handle);
	return NULL;
    }

    plugin = newPlugin(handle, pName, *plugin_version);

    /* Register plugin before it's possibly unloaded */
    registerPlugin(plugin);

    if (plugin_deps) {
	plugin_dep_t *deps = plugin_deps;
	while (deps->name) {
	    plugin_t *d;
	    PSID_log(PSID_LOG_PLUGIN, "%s:   requires '%s' version %d\n",
		     __func__, deps->name, deps->version);
	    d = loadPlugin(deps->name, deps->version, plugin, logfile);
	    if (!d || addRef(&plugin->depends, d) < 0) {
		plugin->finalized = true;
		unloadPlugin(plugin);
		return NULL;
	    }
	    deps++;
	}
    }

    if (plugin->initialize) {
	int ret = plugin->initialize(logfile);

	if (ret) {
	    plugin->finalized = true;
	    unloadPlugin(plugin);
	    return NULL;
	}
    }
    gettimeofday(&plugin->load, NULL);

    if (addRef(&plugin->triggers, trigger ? trigger : plugin) < 0)  {
	PSID_log(-1, "%s: addTrigger() failed\n", __func__);
	finalizePlugin(plugin);
	unloadPlugin(plugin);
	return NULL;
    }

    return plugin;
}

void PSIDplugin_sendList(PStask_ID_t dest)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_INFORESPONSE,
	    .sender = PSC_getMyTID(),
	    .dest = dest,
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_INFO_QUEUE_PLUGINS };

    list_t *p;
    list_for_each(p, &pluginList) {
	plugin_t *plugin = list_entry(p, plugin_t, next);
	size_t len;

	if (plugin->unload) continue;

	if (plugin->finalized) {
	    snprintf(msg.buf, sizeof(msg.buf), "%16s D %3d  ",
		     plugin->name, plugin->version);
	} else {
	    plugin_ref_t *explicit = findRef(&plugin->triggers, plugin);
	    snprintf(msg.buf, sizeof(msg.buf), "%16s %1s %3d  ",
		     plugin->name, explicit ? "*" : " ", plugin->version);
	}
	if (!list_empty(&plugin->triggers)) {
	    printRefList(msg.buf + strlen(msg.buf),
			      sizeof(msg.buf) - strlen(msg.buf),
			      &plugin->triggers);
	}

	len = strlen(msg.buf)+1;
	msg.header.len += len;
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    return;
	}
	msg.header.len -= len;

	msg.type = PSP_INFO_QUEUE_SEP;
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    return;
	}
	msg.type = PSP_INFO_QUEUE_PLUGINS;
    }

    return;
}

void *PSIDplugin_getHandle(char *pName)
{
    plugin_t *plugin = findPlugin(pName);

    if (plugin) return plugin->handle;

    return NULL;
}

/**
 * @brief Unload plugin
 *
 * Unload the plugin @a plugin. For this the plugin's cleanup-method
 * is called, if available. This prompts the plugin to do all cleanup
 * necessary before actually evicting the plugin from address-space
 * via dlclose(). This includes free()ing memory segments allocated by
 * the plugin via malloc(), un-registering of timer, message-handler
 * and selectors, etc.
 *
 * Afterward or if the plugin does not expose a cleanup-method the
 * plugin is marked to get evicted from address-space via
 * dlclose(). The actual action will be performed from within the
 * main-loop.
 *
 * @param plugin The plugin to unload
 *
 * @return If @a plugin is NULL, -1 is returned. Otherwise 0 is
 * returned.
 */
static int unloadPlugin(plugin_t *plugin)
{
    if (!plugin) return -1;

    PSID_log(PSID_LOG_PLUGIN, "%s(%s)\n", __func__, plugin->name);

    if (!plugin->finalized) {
	PSID_log(-1, "%s: plugin '%s' not finalized\n", __func__, plugin->name);
	finalizePlugin(plugin);
    }

    /* This has to be after the call to finalizePlugin(), since
      * unloadPlugin() might be called recursively from therein. */
    if (plugin->unload) return 0;

    if (plugin->cleanup) plugin->cleanup();

    plugin->unload = true;

    return 0;
}

/**
 * @brief Finalize plugin
 *
 * Trigger the plugin @a plugin to get finalized. This is the standard
 * way to safely unload a plugin in a graceful way. The plugin will
 * not be affected, if the plugin is still triggered by another plugin
 * depending on it. Basically, this function just removes the
 * self-trigger of the plugin, i.e. a trigger of the plugin pointing
 * to itself, if the plugin was loaded explicitly. If this was the
 * plugin's last trigger, further measures will be taken in order to
 * actually unload the plugin @a name.
 *
 * If the plugin exposes the function-symbol @a finalize, this method
 * will be called. It is expected that the @a finalize method will do
 * all necessary cleanup that has to be done in an asynchronous way
 * (detaching from a service, etc.) before the plugin itself triggers
 * the actual unload by calling @ref PSIDplugin_unload(). This gives a
 * plugin the chance to cleanup properly before it is evicted from the
 * address-space via dlclose().
 *
 * If no @a finalize method is exposed by the plugin @a name, calling
 * this function behaves exactly like calling @ref
 * PSIDplugin_unload(). Thus, the plugin will be marked to be unloaded
 * immediately, if it is no longer required by other plugins depending
 * on it.
 *
 * @param plugin The plugin to be finalized
 *
 * @return If @a plugin is NULL, -1 is returned. Otherwise 0 is
 * returned.
 */
static int finalizePlugin(plugin_t *plugin)
{
    if (!plugin) return -1;

    PSID_log(PSID_LOG_PLUGIN, "%s(%s)\n", __func__, plugin->name);

    if (plugin->finalized) {
	PSID_log(-1, "%s: plugin '%s' already finalized\n", __func__,
		 plugin->name);
	return 0;
    }

    if (!list_empty(&plugin->triggers)) {
	PSID_log(-1, "%s: plugin '%s' still triggered\n", __func__,
		 plugin->name);
	return 0;
    }

    plugin->finalized = true;

    if (!plugin->finalize) return unloadPlugin(plugin);

    plugin->finalize();
    if (timerisset(&plugin->grace)) {
	struct timeval now, grace = {unloadTimeout, 0};

	PSID_log(PSID_LOG_PLUGIN, "%s: setting grace on '%s'\n", __func__,
		 plugin->name);
	gettimeofday(&now, NULL);
	timeradd(&now, &grace, &plugin->grace);
    }

    return 0;
}

/** Flag detection of loops in the dependency-graph of the plugins */
static bool depLoopDetect = false;

/**
 * @brief Walk dependency graph
 *
 * Walk the graph of dependencies for the plugin @a plugin to unload
 * it forcefully. This function will be called recursively to fully
 * walk the dependency-graph. Once a root of the graph is reached,
 * i.e. a plugin is handled that has no triggers besides being loaded
 * explicitly, this plugin is finalized and a timeout for unloading
 * as defined by @ref unloadTimeout is set.
 *
 * At the same time this function implements a modification of the
 * Dijkstra algorithm determining the distance of each plugin to the
 * original plugin. For this, @a distance is increased for each level
 * of recursion. This distance might be used to identify the plugin
 * to forcefully unload, i.e. to unload while ignoring existing
 * dependencies, in the case of a loop in the dependency-graph.
 *
 * While walking the graph all plugins are identified that are ready
 * for finalization because all triggers are either already finalized
 * or are ready for finalization, too. Such plugins are marked as
 * "cleared".
 *
 * If the algorithm detects a loop in the dependency-graph, the global
 * flag @ref depLoopDetect is raised. This function cannot resolve
 * such loops. They have to be broken up explicitly outside this
 * function.
 *
 * @attention Breaking up loops might crash the daemon. In general,
 * dependency-loops a unnecessary and shall be avoided. Instead,
 * either combine the plugin depending each other into one plugin or
 * extract parts depending on each other into an extra plugin.
 *
 * @param plugin The plugin to handle
 *
 * @param distance The distance of the current plugin from the
 * original plugin to be unloaded
 *
 * @return If @a plugin is not defined, -1 is returned. Otherwise 0 is
 * returned.
 */
static int walkDepGraph(plugin_t *plugin, int distance)
{
    list_t *t;

    if (!plugin) return -1;

    PSID_log(PSID_LOG_PLUGIN, "%s(%s, %d)\n", __func__, plugin->name, distance);

    if (plugin->unload) {
	PSID_log(PSID_LOG_PLUGIN, "%s: %s ready for unload\n", __func__,
		 plugin->name);
	plugin->cleared = true;
	return 0;
    }

    if (plugin->cleared) {
	PSID_log(PSID_LOG_PLUGIN, "%s: %s already cleared\n", __func__,
		 plugin->name);
	return 0;
    }

    if (plugin->distance && plugin->distance < distance) {
	PSID_log(PSID_LOG_PLUGIN, "%s: %s already has distance %d\n", __func__,
		 plugin->name, plugin->distance);
	return 0;
    }

    plugin->distance = distance;

    if (plugin->finalized) {
	if (!timerisset(&plugin->grace)) {
	    struct timeval now, grace = {unloadTimeout, 0};

	    PSID_log(PSID_LOG_PLUGIN, "%s: setting grace on '%s'\n", __func__,
		     plugin->name);
	    gettimeofday(&now, NULL);
	    timeradd(&now, &grace, &plugin->grace);
	}
	plugin->cleared = true;

	return 0;
    }

    /* Trigger grace period on actual finalize */
    gettimeofday(&plugin->grace, NULL);

    remTrigger(plugin, plugin);

    if (list_empty(&plugin->triggers)) {
	PSID_log(PSID_LOG_PLUGIN, "%s: root %s reached\n", __func__,
		 plugin->name);
	PSIDplugin_finalize(plugin->name);

	plugin->cleared = true;

	return 0;
    } else {
	bool cleared = true;
	list_for_each(t, &plugin->triggers) {
	    plugin_ref_t *ref = list_entry(t, plugin_ref_t, next);

	    if (ref->plugin->unload) continue;

	    PSID_log(PSID_LOG_PLUGIN, "%s: forcing %s\n", __func__,
		     ref->plugin->name);
	    walkDepGraph(ref->plugin, distance+1);

	    cleared &= ref->plugin->cleared;
	}

	plugin->cleared = cleared;
	if (!cleared) depLoopDetect = true;
    }

    return 0;
}

/**
 * @brief Find plugin with maximum distance
 *
 * After running @ref walkDepGraph() this function might be used to
 * identify the optimal candidate to forcefully unload for breaking up
 * a dependency-loop explicitly. For that the plugin with maximum
 * distance to the original plugin not yet "cleared" is
 * identified. For a detailed discussion of "cleared" plugins refer to
 * @ref walkDepGraph().
 *
 * @return Returns the plugin with maximum distance to the original
 * plugin not yet cleared. If no plugin is connected to the original
 * plugin or all such plugins are already cleared NULL is returned.
 */
static plugin_t *findMaxDistPlugin(void)
{
    plugin_t *maxDist = NULL;
    list_t *p;

    list_for_each(p, &pluginList) {
	plugin_t *plugin = list_entry(p, plugin_t, next);
	PSID_log(PSID_LOG_PLUGIN, "%s: %s dist %d cleared %d\n", __func__,
		 plugin->name, plugin->distance, plugin->cleared);
	if (plugin->distance && !plugin->cleared
	    && (!maxDist || plugin->distance > maxDist->distance)) {
	    maxDist = plugin;
	}
    }

    return maxDist;
}

/**
 * @brief Unload plugin forcefully
 *
 * Forcefully unload the plugin @a pName. For this, the plugin's
 * dependency-graph is walked in order to evict all plugins it depends
 * on in a direct or indirect way. Walking the dependency-graph is done
 * by calling @ref walkDepGraph().
 *
 * If a dependency-loop is flagged via @ref depLoopDetect, a victim is
 * determined by calling @ref findMaxDistPlugin(). This victim is
 * forcefully evicted from address-space.
 *
 * The two steps described -- walking the graph and breaking up loops
 * -- are repeated in an iterative way until all loops are broken and
 * the original plugin is ready to become unloaded.
 *
 * Actually the plugins will not be unloaded immediately but just
 * prepared to become unloaded. As soon as all finalization of the
 * triggers have timed-out and became in fact unloaded the plugin
 * itself will be finalized and unloaded afterward.
 *
 * @attention Breaking up dependency-loops might crash the daemon. In
 * general, dependency-loops a unnecessary and shall be
 * avoided. Instead, either combine the plugin depending each other
 * into one plugin or extract parts depending on each other into an
 * extra plugin.
 *
 * @param pName Name of the plugin to unload
 *
 * @return If the plugin is not found, -1 is returned. Otherwise 0 is
 * returned.
 */
static int forceUnloadPlugin(char *pName)
{
    plugin_t *plugin = findPlugin(pName);
    int rounds = 0;

    if (!plugin) return -1;

    do {
	depLoopDetect = false;

	walkDepGraph(plugin, 1);

	if (depLoopDetect) {
	    plugin_t *victim = findMaxDistPlugin();
	    list_t *t, *tmp;

	    if (!victim) {
		PSID_log(-1, "%s: no victim found despite of loop\n", __func__);
		return -1;
	    }

	    PSID_log(-1, "%s: kick out victim '%s' (distance %d) forcefully\n",
		     __func__, victim->name, victim->distance);

	    list_for_each_safe(t, tmp, &victim->triggers) {
		plugin_ref_t *ref = list_entry(t, plugin_ref_t, next);

		remDepend(ref->plugin, victim);
		remTrigger(victim, ref->plugin);
	    }
	    victim->cleared = true;
	}
	rounds++;
    } while (depLoopDetect);

    PSID_log((rounds > 1) ? -1 : PSID_LOG_PLUGIN,
	     "%s: %d rounds of graph-walk required\n", __func__, rounds);

    return 0;
}

int PSIDplugin_finalize(char *pName)
{
    plugin_t *plugin = findPlugin(pName);

    if (!plugin) return -1;

    remTrigger(plugin, plugin);

    return plugin->finalized;
}

int PSIDplugin_unload(char *pName)
{
    plugin_t *plugin = findPlugin(pName);

    if (!plugin) return -1;

    return unloadPlugin(plugin);
}

void PSIDplugin_forceUnloadAll(void)
{
    list_t *p;

    list_for_each(p, &pluginList) {
	plugin_t *plugin = list_entry(p, plugin_t, next);

	if (timerisset(&plugin->grace) || plugin->unload) continue;

	forceUnloadPlugin(plugin->name);
    }
}

static void sendStr(DDTypedBufferMsg_t *msg, char *str, const char *caller)
{
    bool first = true;

    if (!str) return;

    while (*str || first) {
	size_t num = MIN(strlen(str), sizeof(msg->buf) - 1);

	first = false;
	memcpy(msg->buf, str, num);
	msg->buf[num] = '\0';

	msg->header.len += num+1;
	if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_warn(-1, errno, "%s: %s: sendMsg()", caller ? caller : "<?>",
		      __func__);
	    break;
	}
	msg->header.len -= num+1;

	str += num;
    }
}

static void sendAvail(PStask_ID_t dest)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGINRES,
	    .dest = dest,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_PLUGIN_AVAIL };
    char dirName[PATH_MAX], *instDir, res[256] = { '\0' };
    DIR *dir;
    struct dirent *dent;

    instDir = getenv("PSID_PLUGIN_PATH");
    if (!instDir) instDir = PSC_lookupInstalldir(NULL);
    if (!instDir) {
	PSID_log(-1, "%s: installation directory not found\n", __func__);
	snprintf(res, sizeof(res), "installation directory not found\n");
	goto end;
    }
    snprintf(dirName, sizeof(dirName), "%s/plugins", instDir);

    if (!(dir = opendir(dirName))) {
	int eno = errno;
	PSID_warn(-1, eno, "%s: opendir(%s) failed", __func__, dirName);
	snprintf(res, sizeof(res), "opendir(%.200s) failed: %s\n", dirName,
		 strerror(eno));
	goto end;
    }

    rewinddir(dir);
    while ((dent = readdir(dir))) {
	char *nameStr = dent->d_name;
	size_t nameLen = PSP_strLen(nameStr);

	if (nameLen && !strcmp(&nameStr[nameLen - 4], ".so")) {
	    nameStr[nameLen - 4] = '\n';
	    nameStr[nameLen - 3] = '\0';

	    sendStr(&msg, nameStr, __func__);
	}
    }
    closedir(dir);

end:
    if (*res) sendStr(&msg, res, __func__);

    /* Create stop message */
    sendStr(&msg, "", __func__);
}

static void sendHelp(PStask_ID_t dest, char *pName)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGINRES,
	    .dest = dest,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_PLUGIN_HELP };
    plugin_t *plugin = findPlugin(pName);

    if (!plugin) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: unknown plugin '%s'\n",
		 __func__, pName);
	sendStr(&msg, mBuf, __func__);
    } else if (!plugin->help) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: no help-method for '%s' \n",
		 __func__, pName);
	sendStr(&msg, mBuf, __func__);
    } else {
	char *res = plugin->help();

	if (res) {
	    sendStr(&msg, res, __func__);
	    free(res);
	}
    }

    /* Create stop message */
    sendStr(&msg, "", __func__);
}

static void handleSetKey(PStask_ID_t dest, char *buf)
{
    char *pName = buf, *key = pName + PSP_strLen(pName);
    char *val = key + PSP_strLen(key);
    plugin_t *plugin = findPlugin(pName);
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGINRES,
	    .dest = dest,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_PLUGIN_SET };


    if (!plugin) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: unknown plugin '%s'\n",
		 __func__, pName);
	sendStr(&msg, mBuf, __func__);
    } else if (!plugin->set) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: no set-method for '%s' \n",
		 __func__, pName);
	sendStr(&msg, mBuf, __func__);
    } else {
	char *res = plugin->set(key, val);

	if (res) {
	    sendStr(&msg, res, __func__);
	    free(res);
	}
    }

    /* Create stop message */
    sendStr(&msg, "", __func__);
}

static void handleUnsetKey(PStask_ID_t dest, char *buf)
{
    char *pName = buf, *key = pName + PSP_strLen(pName);
    plugin_t *plugin = findPlugin(pName);
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGINRES,
	    .dest = dest,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_PLUGIN_UNSET };

    if (!plugin) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: unknown plugin '%s'\n",
		 __func__, pName);
	sendStr(&msg, mBuf, __func__);
    } else if (!plugin->unset) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: no unset-method for '%s' \n",
		 __func__, pName);
	sendStr(&msg, mBuf, __func__);
    } else {
	char *res = plugin->unset(key);

	if (res) {
	    sendStr(&msg, res, __func__);
	    free(res);
	}
    }

    /* Create stop message */
    sendStr(&msg, "", __func__);
}

static void handleShowKey(PStask_ID_t dest, char *buf)
{
    char *pName = buf, *key = pName + PSP_strLen(pName);
    plugin_t *plugin = findPlugin(pName);
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGINRES,
	    .dest = dest,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_PLUGIN_SHOW };

    if (! *key) key=NULL;

    if (!plugin) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: unknown plugin '%s'\n",
		 __func__, pName);
	sendStr(&msg, mBuf, __func__);
    } else if (!plugin->show) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: no show-method for '%s' \n",
		 __func__, pName);
	sendStr(&msg, mBuf, __func__);
    } else {
	char *res = plugin->show(key);

	if (res) {
	    sendStr(&msg, res, __func__);
	    free(res);
	}
    }

    /* Create stop message */
    sendStr(&msg, "", __func__);
}

static void sendLoadTime(PStask_ID_t dest, plugin_t *plugin)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGINRES,
	    .dest = dest,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_PLUGIN_LOADTIME };
    char mBuf[sizeof(msg.buf)];

    if (!plugin) return;

    snprintf(mBuf, sizeof(mBuf), "\t%10s %4d %s", plugin->name, plugin->version,
	     ctime(&plugin->load.tv_sec));
    sendStr(&msg, mBuf, __func__);
}

static void handleLoadTime(PStask_ID_t dest, char *pName)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGINRES,
	    .dest = dest,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_PLUGIN_LOADTIME };

    if (!pName) return;

    if (*pName) {
	plugin_t *plugin = findPlugin(pName);

	if (!plugin) {
	    char mBuf[sizeof(msg.buf)];
	    snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: plugin '%s' not found\n",
		     __func__, pName);
	    sendStr(&msg, mBuf, __func__);
	    msg.header.len = sizeof(msg.header) + sizeof(msg.type);
	} else {
	    sendLoadTime(dest, plugin);
	}
    } else {
	list_t *p;

	list_for_each(p, &pluginList) {
	    plugin_t *plugin = list_entry(p, plugin_t, next);

	    sendLoadTime(dest, plugin);
	}
    }

    /* Create stop message */
    sendStr(&msg, "", __func__);

    return;
}

int PSIDplugin_getAPIversion(void)
{
    return pluginAPIVersion;
}

/**
 * @brief Handle a PSP_CD_PLUGIN message.
 *
 * Handle the message @a inmsg of type PSP_CD_PLUGIN.
 *
 * With this kind of message a administrator will request to load or
 * remove a plugin. The action is encrypted in the type-part of @a
 * inmsg. The buf-part will hold the name of the plugin.
 *
 * An answer will be sent as an PSP_CD_PLUGINRES message.
 *
 * @param inmsg Pointer to the message to handle
 *
 * @return Always return true
 */
static bool msg_PLUGIN(DDTypedBufferMsg_t *inmsg)
{
    int destID = PSC_getID(inmsg->header.dest), ret = 0;

    PSID_log(PSID_LOG_PLUGIN, "%s(%s, %s)\n", __func__,
	     PSC_printTID(inmsg->header.sender), inmsg->buf);

    if (!PSID_checkPrivilege(inmsg->header.sender)) {
	switch (inmsg->type) {
	case PSP_PLUGIN_AVAIL:
	case PSP_PLUGIN_HELP:
	case PSP_PLUGIN_SHOW:
	case PSP_PLUGIN_LOADTIME:
	    break;
	default:
	    PSID_log(-1, "%s: task %s not allowed to touch plugins\n",
		     __func__, PSC_printTID(inmsg->header.sender));
	    ret = EACCES;
	    goto end;
	}
    }

    if (destID != PSC_getMyID()) {
	if (!PSIDnodes_isUp(destID)) {
	    ret = EHOSTDOWN;
	    goto end;
	}
	if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	    ret = errno;
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    goto end;
	}
	return true; /* destination node will send PLUGINRES message */
    } else {
	switch (inmsg->type) {
	case PSP_PLUGIN_LOAD:
	    if (!loadPlugin(inmsg->buf, 0, NULL, pluginLogfile)) ret = -1;
	    break;
	case PSP_PLUGIN_REMOVE:
	    if (PSIDplugin_finalize(inmsg->buf) < 0) ret = ENODEV;
	    break;
	case PSP_PLUGIN_FORCEREMOVE:
	    if (forceUnloadPlugin(inmsg->buf) < 0) ret = ENODEV;
	    break;
	case PSP_PLUGIN_AVAIL:
	    sendAvail(inmsg->header.sender);
	    return true;
	case PSP_PLUGIN_HELP:
	    sendHelp(inmsg->header.sender, inmsg->buf);
	    return true;
	case PSP_PLUGIN_SET:
	    handleSetKey(inmsg->header.sender, inmsg->buf);
	    return true;
	case PSP_PLUGIN_UNSET:
	    handleUnsetKey(inmsg->header.sender, inmsg->buf);
	    return true;
	case PSP_PLUGIN_SHOW:
	    handleShowKey(inmsg->header.sender, inmsg->buf);
	    return true;
	case PSP_PLUGIN_LOADTIME:
	    handleLoadTime(inmsg->header.sender, inmsg->buf);
	    return true;
	default:
	    PSID_log(-1, "%s: Unknown message type %d\n", __func__,
		     inmsg->type);
	    ret = -1;
	    goto end;
	}
    }

end:
    {
	DDTypedMsg_t msg = {
	    .header = {
		.type = PSP_CD_PLUGINRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = ret };
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	}
    }
    return true;
}

/**
 * @brief Drop a PSP_CD_PLUGIN message
 *
 * Drop the message @a msg of type PSP_CD_PLUGIN.
 *
 * Since the requesting process waits for a reaction to its request a
 * corresponding answer is created.
 *
 * @param msg Pointer to the message to drop
 *
 * @return Always return true
 */
static bool drop_PLUGIN(DDBufferMsg_t *msg)
{
    DDTypedMsg_t typmsg;

    typmsg.header.type = PSP_CD_PLUGINRES;
    typmsg.header.dest = msg->header.sender;
    typmsg.header.sender = PSC_getMyTID();
    typmsg.header.len = sizeof(typmsg);
    typmsg.type = -1;

    sendMsg(&typmsg);
    return true;
}

/**
 * @brief Unload plugin
 *
 * Unload the plugin @a plugin. Unloading a plugin might fail due to
 * still existent dependencies from other plugins. This functions is
 * unable to force unloading a specific plugin.
 *
 * @param plugin The plugin to be unloaded
 *
 * @return Without error, i.e. if the plugin was successfully
 * unloaded, 1 is returned. Or 0, if some error occurred. In the
 * latter case the plugin might still be loaded.
 */
static int doUnload(plugin_t *plugin)
{
    char line[80];
    list_t *d, *tmp;

    if (!plugin || !plugin->handle) return 0;

    PSID_log(PSID_LOG_PLUGIN, "%s(%s)\n", __func__, plugin->name);

    if (!plugin->unload) {
	PSID_log(-1, "%s: plugin '%s' not flagged for unload\n", __func__,
		 plugin->name);
	return 0;
    }

    if (!list_empty(&plugin->triggers)) {
	printRefList(line, sizeof(line), &plugin->triggers);
	PSID_log(-1, "%s: '%s' still triggered by: %s\n", __func__,
		 plugin->name, line);
	return 0;
    }

    /* Remove triggers from plugins we depend on */
    list_for_each_safe(d, tmp, &plugin->depends) {
	plugin_ref_t *ref = list_entry(d, plugin_ref_t, next);

	remTrigger(ref->plugin, plugin);
	remDepend(plugin, ref->plugin);
    }

    /* Actual unload */
    if (dlclose(plugin->handle)) {
	PSID_log(-1, "%s: dlclose(%s): %s\n", __func__, plugin->name,
		 dlerror());
    } else {
	PSID_log(PSID_LOG_PLUGIN, "%s: '%s' successfully unloaded\n", __func__,
		 plugin->name);
    }
    plugin->handle = NULL;

    list_del(&plugin->next);
    delPlugin(plugin);

    return 1;
}

/**
 * @brief Handle plugins in main loop
 *
 * This function collects all actions to be executed asynchronously in
 * the daemon's main loop. Currently this includes
 *
 *   - Unloading plugins via dlclose() when flagged
 *
 *   - Flagging plugin to unload on forceUnload after timeout expired
 *
 * @return No return value
 */
static void handlePlugins(void)
{
    list_t *p, *tmp;
    struct timeval now;

    gettimeofday(&now, NULL);

    list_for_each_safe(p, tmp, &pluginList) {
	plugin_t *plugin = list_entry(p, plugin_t, next);

	if (plugin->finalized && (timerisset(&plugin->grace)
				  && timercmp(&now, &plugin->grace, >))) {
	    PSID_log(PSID_LOG_PLUGIN, "%s: finalize() timed out for %s\n",
		     __func__, plugin->name);

	    unloadPlugin(plugin);
	}

	if (plugin->unload) doUnload(plugin);
    }
}

/**
 * @brief Dummy loop action
 *
 * Dummy function used as a guarding loop action to @ref
 * handlePlugins().
 *
 * Background is that @ref handlePlugins() might implicitly remove
 * loop actions from the corresponding list. Such loop actions were
 * registered by a plugin (e.g. psaccount) that was unloaded and might
 * have been the next one after @ref handlePlugins() in the list of
 * loop actions with a certain probability. Thus, removing this loop
 * action will break the list_for_each_safe in @ref
 * PSID_handleLoopActions() leading to undefined behavior of the psid
 * -- most probably just running into a segmentation fault.
 *
 * By adding this dummy function as a loop action right after @ref
 * handlePlugins() we ensure that its successor in the list will never
 * be removed.
 *
 * @return No return value
 */
static void handlePluginsGuard(void)
{}

void initPlugins(FILE *logfile)
{
    /* Register msg-handlers/droppers for plugin load/unload */
    PSID_registerMsg(PSP_CD_PLUGIN, (handlerFunc_t)msg_PLUGIN);
    PSID_registerMsg(PSP_CD_PLUGINRES, frwdMsg);

    PSID_registerDropper(PSP_CD_PLUGIN, drop_PLUGIN);

    /* Register dummy handler to suppress syslog for not loaded modules */
    PSID_registerMsg(PSP_PLUG_NODEINFO, NULL);

    PSID_registerLoopAct(handlePlugins);
    PSID_registerLoopAct(handlePluginsGuard);

    /* Handle list of plugins found in the configuration file */
    list_t *p, *tmp;
    list_for_each_safe(p, tmp, &PSID_config->plugins) {
	nameList_t *ent = list_entry(p, nameList_t, next);

	list_del(&ent->next);
	if (!ent->name || ! *ent->name) {
	    PSID_log(-1, "%s: No name given\n", __func__);
	    free(ent);
	    continue;
	}

	PSID_log(PSID_LOG_PLUGIN, "%s: load '%s'\n", __func__, ent->name);
	if (!loadPlugin(ent->name, 0, NULL, logfile)) {
	    PSID_log(-1, "%s: loading '%s' failed.\n", __func__, ent->name);
	}
	if (ent->name) free(ent->name);
	free(ent);
    }

    /* Store logfile for plugins loaded during runtime, too */
    pluginLogfile = logfile;
}
