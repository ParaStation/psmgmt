/*
 * ParaStation
 *
 * Copyright (C) 2009-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psidplugin.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "config_parsing.h"
#include "list.h"
#include "plugin.h"
#include "pscommon.h"
#include "psprotocol.h"
#include "pspluginprotocol.h"
#include "psserial.h"
#include "psidutil.h"
#include "psidcomm.h"
#include "psidnodes.h"

typedef int initFunc_t(FILE *logfile);

typedef void voidFunc_t(void);

typedef char *setFunc_t(char *key, char *value);

typedef char *keyFunc_t(char *key);

/** Structure holding all information concerning a plugin */
struct PSIDplugin {
    list_t next;             /**< Used to put into @ref pluginList */
    list_t triggers;         /**< List of plugins triggering this one */
    list_t depends;          /**< List of plugins this one depends on */
    void *handle;            /**< Handle created by dlopen() */
    char *name;              /**< Actual name */
    initFunc_t *initialize;  /**< Initializer (after dependencies resolved) */
    voidFunc_t *finalize;    /**< Finalize (trigger plugin's stop) */
    voidFunc_t *cleanup;     /**< Cleanup (immediately before unload) */
    keyFunc_t *help;         /**< Some help message from the plugin */
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
};

/**
 * Structure used to create @a trigger and @a depends members of @ref
 * PSIDplugin_t
 */
typedef struct {
    PSIDplugin_t plugin; /**< Corresponding plugin added to the list */
    list_t next;         /**< Actual list entry */
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
 *
 * 133: Message handling via registered handlers in psidforwarder
 *
 * 134: Reworked hook PSIDHOOK_FRWRD_CLNT_RLS to support multiple plugins
 *
 * 135: Add key parameter to help() function
 *
 * 136: new hook PSIDHOOK_PELOGUE_DROP
 *
 * 137: new hook PSIDHOOK_SPAWN_TASK
 *
 * 138: new hook PSIDHOOK_FRWRD_SETUP
 *
 * 139: new hooks PSIDHOOK_NODE_UNKNOWN, PSIDHOOK_SENDER_UNKNOWN
 *
 * 140: new hooks PSIDHOOK_LAST_CHILD_GONE, PSIDHOOK_LAST_RESRELEASED
 *
 * 141: new hook PSIDHOOK_EXEC_CLIENT_EXEC
 *
 * 142: new hooks PSIDHOOK_JOBCOMPLETE, PSIDHOOK_FILL_RESFINALIZED
 *
 * 143: new hooks PSIDHOOK_PRIV_FRWRD_CLNT_RES, PSIDHOOK_PRIV_FRWRD_INIT
 *
 * 144: reworked hook PSIDHOOK_SHUTDOWN to tell the current phase
 *
 * 145: new PSIDHOOK_RECEIVEPART
 */
static int pluginAPIVersion = 145;


/** Grace period between finalize and unload on forcefully unloads */
static int unloadTimeout = 4;

/** logfile to be used by all plugins (unless they decide otherwise) */
static FILE *pluginLogfile = NULL;

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
static plugin_ref_t * findRef(list_t *refList, PSIDplugin_t plugin)
{
    if (!refList || !plugin) return NULL;

    list_t *t;
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
static int addRef(list_t *refList, PSIDplugin_t plugin)
{
    if (findRef(refList, plugin)) return 0;

    plugin_ref_t *ref = getRef();
    if (!ref) {
	PSID_fwarn(errno, "getRef()");
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
static PSIDplugin_t remRef(list_t *refList, PSIDplugin_t plugin)
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
static PSIDplugin_t remDepend(PSIDplugin_t plugin, PSIDplugin_t depend)
{
    if (!plugin || !depend) return NULL;

    PSIDplugin_t removed = remRef(&plugin->depends, depend);
    if (!removed) {
	PSID_flog("dependency '%s' not found in '%s'\n",
		  depend->name, plugin->name);
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

PSIDplugin_t PSIDplugin_find(char *pName)
{
    if (!pName || ! *pName) return NULL;

    list_t *p;
    list_for_each(p, &pluginList) {
	PSIDplugin_t plugin = list_entry(p, struct PSIDplugin, next);

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
static PSIDplugin_t newPlugin(void *handle, char *pName, int pVer)
{
    PSID_fdbg(PSID_LOG_PLUGIN, "(%p, %s, %d)\n", handle, pName, pVer);

    PSIDplugin_t plugin = malloc(sizeof(*plugin));
    if (!plugin) {
	PSID_fwarn(errno, "malloc()");
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
static void delPlugin(PSIDplugin_t plugin)
{
    char line[80];

    if (!plugin) return;

    if (!list_empty(&plugin->triggers)) {
	printRefList(line, sizeof(line), &plugin->triggers);
	PSID_flog("'%s' still triggered by: %s\n", plugin->name, line);
	return;
    }
    if (!list_empty(&plugin->depends)) {
	printRefList(line, sizeof(line), &plugin->depends);
	PSID_flog("'%s' still depends on: %s\n", plugin->name, line);
	return;
    }
    free(plugin->name);
    if (plugin->handle) PSID_flog("handle %p still exists\n", plugin->handle);

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
static PSIDplugin_t registerPlugin(PSIDplugin_t new)
{
    PSID_fdbg(PSID_LOG_PLUGIN, "'%s' ver %d\n", new->name, new->version);

    PSIDplugin_t plugin = PSIDplugin_find(new->name);
    if (plugin) {
	PSID_flog("'%s' already registered with version %d\n",
		  plugin->name, plugin->version);
	return plugin;
    }

    list_add(&new->next, &pluginList);

    return NULL;
}

static int unloadPlugin(PSIDplugin_t plugin);

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
static int finalizePlugin(PSIDplugin_t plugin)
{
    if (!plugin) return -1;

    PSID_fdbg(PSID_LOG_PLUGIN, "%s\n", plugin->name);

    if (plugin->finalized) {
	PSID_flog("plugin '%s' already finalized\n", plugin->name);
	return 0;
    }

    if (!list_empty(&plugin->triggers)) {
	PSID_flog("plugin '%s' still triggered\n", plugin->name);
	return 0;
    }

    plugin->finalized = true;

    if (!plugin->finalize) return unloadPlugin(plugin);

    plugin->finalize();
    if (timerisset(&plugin->grace) && !plugin->unload) {
	struct timeval now, grace = {unloadTimeout, 0};

	PSID_fdbg(PSID_LOG_PLUGIN, "setting grace on '%s'\n", plugin->name);
	gettimeofday(&now, NULL);
	timeradd(&now, &grace, &plugin->grace);
    }

    return 0;
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
static int unloadPlugin(PSIDplugin_t plugin)
{
    if (!plugin) return -1;

    PSID_fdbg(PSID_LOG_PLUGIN, "%s\n", plugin->name);

    if (!plugin->finalized) {
	PSID_flog("plugin '%s' not finalized\n", plugin->name);
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
static PSIDplugin_t remTrigger(PSIDplugin_t plugin, PSIDplugin_t trigger)
{
    if (!plugin || !trigger) return NULL;

    PSIDplugin_t removed = remRef(&plugin->triggers, trigger);
    if (!removed) {
	PSID_fdbg(plugin == trigger ? PSID_LOG_PLUGIN : -1,
		  "trigger '%s' not found in '%s'\n",
		  trigger->name, plugin->name);
	return NULL;
    } else {
	PSID_fdbg(PSID_LOG_PLUGIN, "removed '%s' from '%s'\n",
		  trigger->name, plugin->name);
    }

    if (list_empty(&plugin->triggers)) finalizePlugin(plugin);

    return removed;
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
static int doUnload(PSIDplugin_t plugin)
{
    if (!plugin || !plugin->handle) return 0;

    PSID_fdbg(PSID_LOG_PLUGIN, "%s\n", plugin->name);

    if (!plugin->unload) {
	PSID_flog("plugin '%s' not flagged for unload\n", plugin->name);
	return 0;
    }

    if (!list_empty(&plugin->triggers)) {
	char line[80];
	printRefList(line, sizeof(line), &plugin->triggers);
	PSID_flog("'%s' still triggered by: %s\n", plugin->name, line);
	return 0;
    }

    /* Remove triggers from plugins we depend on */
    list_t *d, *tmp;
    list_for_each_safe(d, tmp, &plugin->depends) {
	plugin_ref_t *ref = list_entry(d, plugin_ref_t, next);

	remTrigger(ref->plugin, plugin);
	remDepend(plugin, ref->plugin);
    }

    /* Actual unload */
    if (dlclose(plugin->handle)) {
	PSID_flog("dlclose(%s): %s\n", plugin->name, dlerror());
    } else {
	PSID_fdbg(PSID_LOG_PLUGIN, "'%s' successfully unloaded\n", plugin->name);
    }
    plugin->handle = NULL;

    list_del(&plugin->next);
    delPlugin(plugin);

    return 1;
}

int PSIDplugin_getUnloadTmout(void)
{
    return unloadTimeout;
}

void PSIDplugin_setUnloadTmout(int tmout)
{
    if (tmout < 0) {
	PSID_flog("illegal value for timeout: %d\n", tmout);
    } else {
	unloadTimeout = tmout;
    }
}

int PSIDplugin_getNum(void)
{
    int num = 0;

    list_t *p;
    list_for_each(p, &pluginList) {
	PSIDplugin_t plugin = list_entry(p, struct PSIDplugin, next);
	if (!plugin->unload) num++;
    }

    return num;
}

PSIDplugin_t PSIDplugin_load(char *pName, int minVer,
			     PSIDplugin_t trigger, FILE *logfile)
{
    char filename[PATH_MAX];

    PSIDplugin_t plugin = PSIDplugin_find(pName);

    if (!pName || ! *pName) {
	PSID_flog("no name given\n");
	return NULL;
    }

    PSID_fdbg(PSID_LOG_PLUGIN, "(%s, %d, %s)\n", pName, minVer,
	      trigger ? trigger->name : "<no trigger>");

    if (plugin) {
	PSID_fdbg(PSID_LOG_PLUGIN, "version %d already loaded\n",
		  plugin->version);
	if (plugin->version < minVer) {
	    PSID_flog("version %d of '%s' too small: %d required\n",
		      plugin->version, plugin->name, minVer);
	    return NULL;
	}

	if (plugin->finalized) {
	    PSID_flog("plugin '%s' already finalized\n", plugin->name);
	    return NULL;
	}

	if (addRef(&plugin->triggers, trigger ? trigger : plugin) < 0) {
	    return NULL;
	}
	if (trigger && addRef(&trigger->depends, plugin) < 0) {
	    remRef(&plugin->triggers, trigger);
	    return NULL;
	}

	return plugin;
    }

    char *instDir = getenv("PSID_PLUGIN_PATH");
    if (!instDir) instDir = PSC_lookupInstalldir(NULL);
    if (!instDir) {
	PSID_flog("installation directory not found\n");
	return NULL;
    }
    snprintf(filename, sizeof(filename), "%s/plugins/%s.so", instDir, pName);
    void *handle = dlopen(filename, RTLD_NOW);

    if (!handle) {
	PSID_flog("dlopen(%s) failed: %s\n", filename, dlerror());
	return NULL;
    }

    int *plugin_reqAPI = dlsym(handle, "requiredAPI");
    char *plugin_name = dlsym(handle, "name");
    int *plugin_version = dlsym(handle, "version");
    plugin_dep_t *plugin_deps = dlsym(handle, "dependencies");

    if (!plugin_reqAPI) {
	PSID_fdbg(PSID_LOG_PLUGIN, "any API accepted\n");
    } else {
	PSID_fdbg(PSID_LOG_PLUGIN, "API version %d or above required\n",
		  *plugin_reqAPI);
    }
    if (plugin_reqAPI && pluginAPIVersion < *plugin_reqAPI) {
	PSID_flog("'%s' needs API version %d or above. This is %d\n", pName,
		  *plugin_reqAPI, pluginAPIVersion);
	dlclose(handle);
	return NULL;
    }

    if (!plugin_name || !*plugin_name) {
	PSID_flog("plugin-file '%s' does not define name\n", filename);
	dlclose(handle);
	return NULL;
    } else if (strcmp(plugin_name, pName)) {
	PSID_flog("WARNING: plugin_name '%s' and name '%s' differ\n",
		  plugin_name, pName);
    }

    if (!plugin_version) {
	PSID_flog("cannot determine version of '%s'\n", pName);
	dlclose(handle);
	return NULL;
    }

    PSID_fdbg(PSID_LOG_PLUGIN, "plugin_version is %d\n", *plugin_version);

    if (minVer && *plugin_version < minVer) {
	PSID_flog("version %d or above of '%s' required. This is %d\n",
		  minVer, pName, *plugin_version);
	dlclose(handle);
	return NULL;
    }

    plugin = newPlugin(handle, pName, *plugin_version);

    /* Register plugin before it's possibly unloaded */
    registerPlugin(plugin);

    if (plugin_deps) {
	for (plugin_dep_t *deps = plugin_deps; deps->name; deps++) {
	    PSID_fdbg(PSID_LOG_PLUGIN, "  requires '%s' version %d\n",
		      deps->name, deps->version);
	    PSIDplugin_t d = PSIDplugin_load(deps->name, deps->version, plugin,
					     logfile);
	    if (!d) {
		plugin->finalized = true;
		unloadPlugin(plugin);
		return NULL;
	    }
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
	PSID_flog("adding trigger failed\n");
	finalizePlugin(plugin);
	unloadPlugin(plugin);
	return NULL;
    }
    if (trigger && addRef(&trigger->depends, plugin) < 0)  {
	PSID_flog("adding dependency at trigger failed\n");
	remRef(&plugin->triggers, trigger);
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
	    .len = DDTypedBufMsgOffset },
	.type = PSP_INFO_QUEUE_PLUGINS };

    list_t *p;
    list_for_each(p, &pluginList) {
	PSIDplugin_t plugin = list_entry(p, struct PSIDplugin, next);
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
	    PSID_fwarn(errno, "sendMsg()");
	    return;
	}
	msg.header.len -= len;

	msg.type = PSP_INFO_QUEUE_SEP;
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg()");
	    return;
	}
	msg.type = PSP_INFO_QUEUE_PLUGINS;
    }

    return;
}

void *PSIDplugin_getHandle(char *pName)
{
    PSIDplugin_t plugin = PSIDplugin_find(pName);
    if (plugin) return plugin->handle;

    return NULL;
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
static int walkDepGraph(PSIDplugin_t plugin, int distance)
{
    if (!plugin) return -1;

    PSID_fdbg(PSID_LOG_PLUGIN, "(%s, %d)\n", plugin->name, distance);

    if (plugin->unload) {
	PSID_fdbg(PSID_LOG_PLUGIN, "%s ready for unload\n", plugin->name);
	plugin->cleared = true;
	return 0;
    }

    if (plugin->cleared) {
	PSID_fdbg(PSID_LOG_PLUGIN, "%s already cleared\n", plugin->name);
	return 0;
    }

    if (plugin->distance && plugin->distance < distance) {
	PSID_fdbg(PSID_LOG_PLUGIN, "%s already has distance %d\n",
		  plugin->name, plugin->distance);
	return 0;
    }

    plugin->distance = distance;

    if (plugin->finalized) {
	if (!timerisset(&plugin->grace)) {
	    struct timeval now, grace = {unloadTimeout, 0};

	    PSID_fdbg(PSID_LOG_PLUGIN, "setting grace on '%s'\n", plugin->name);
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
	PSID_fdbg(PSID_LOG_PLUGIN, "root %s reached\n", plugin->name);
	plugin->cleared = true;

	return 0;
    } else {
	bool cleared = true;
	list_t *t, *tmp;
	list_for_each_safe(t, tmp, &plugin->triggers) {
	    plugin_ref_t *ref = list_entry(t, plugin_ref_t, next);

	    if (ref->plugin->unload) continue;

	    PSID_fdbg(PSID_LOG_PLUGIN, "forcing %s\n", ref->plugin->name);
	    walkDepGraph(ref->plugin, distance + 1);

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
static PSIDplugin_t findMaxDistPlugin(void)
{
    PSIDplugin_t maxDist = NULL;

    list_t *p;
    list_for_each(p, &pluginList) {
	PSIDplugin_t plugin = list_entry(p, struct PSIDplugin, next);
	PSID_fdbg(PSID_LOG_PLUGIN, "%s dist %d cleared %d\n",
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
    PSIDplugin_t plugin = PSIDplugin_find(pName);
    if (!plugin) return -1;

    int rounds = 0;
    do {
	depLoopDetect = false;

	walkDepGraph(plugin, 1);

	if (depLoopDetect) {
	    PSIDplugin_t victim = findMaxDistPlugin();
	    if (!victim) {
		PSID_flog("no victim found despite of loop\n");
		return -1;
	    }

	    PSID_flog("kick out victim '%s' (distance %d) forcefully\n",
		      victim->name, victim->distance);

	    list_t *t, *tmp;
	    list_for_each_safe(t, tmp, &victim->triggers) {
		plugin_ref_t *ref = list_entry(t, plugin_ref_t, next);

		remDepend(ref->plugin, victim);
		remTrigger(victim, ref->plugin);
	    }
	    victim->cleared = true;
	}
	rounds++;
    } while (depLoopDetect);

    PSID_fdbg((rounds > 1) ? -1 : PSID_LOG_PLUGIN,
	      "%d rounds of graph-walk required\n", rounds);

    return 0;
}

int PSIDplugin_finalize(char *pName)
{
    PSIDplugin_t plugin = PSIDplugin_find(pName);
    if (!plugin) return -1;

    remTrigger(plugin, plugin);

    return plugin->finalized;
}

int PSIDplugin_unload(char *pName)
{
    PSIDplugin_t plugin = PSIDplugin_find(pName);
    if (!plugin) return -1;

    return unloadPlugin(plugin);
}

void PSIDplugin_finalizeAll(void)
{
    for (bool rerun = true; rerun;) {
	PSID_fdbg(PSID_LOG_PLUGIN, "=====================================\n");
	rerun = false;
	list_t *p, *tmp;
	list_for_each_safe(p, tmp, &pluginList) {
	    PSIDplugin_t plugin = list_entry(p, struct PSIDplugin, next);
	    if (!plugin->finalized) remTrigger(plugin, plugin);
	    if (plugin->unload) {
		doUnload(plugin);
		rerun = true;
	    }
	}
    }
}

void PSIDplugin_forceUnloadAll(void)
{
    list_t *p;
    list_for_each(p, &pluginList) {
	PSIDplugin_t plugin = list_entry(p, struct PSIDplugin, next);

	if (timerisset(&plugin->grace) || plugin->unload) continue;

	forceUnloadPlugin(plugin->name);
    }
}

static void sendStr(DDTypedBufferMsg_t *msg, char *str, const char *caller)
{
    if (!str) return;

    bool first = true;
    while (*str || first) {
	size_t num = MIN(strlen(str), sizeof(msg->buf) - 1);

	first = false;
	memcpy(msg->buf, str, num);
	msg->buf[num] = '\0';

	msg->header.len += num+1;
	if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "%s: sendMsg()", caller ? caller : "<?>");
	    break;
	}
	msg->header.len -= num+1;

	str += num;
    }
}

static void sendAvailOld(PStask_ID_t dest)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGINRES,
	    .dest = dest,
	    .sender = PSC_getMyTID(),
	    .len = DDTypedBufMsgOffset },
	.type = PSP_PLUGIN_AVAIL };
    char dirName[PATH_MAX], *instDir, res[256] = { '\0' };
    DIR *dir;
    struct dirent *dent;

    instDir = getenv("PSID_PLUGIN_PATH");
    if (!instDir) instDir = PSC_lookupInstalldir(NULL);
    if (!instDir) {
	PSID_flog("installation directory not found\n");
	snprintf(res, sizeof(res), "installation directory not found\n");
	goto end;
    }
    snprintf(dirName, sizeof(dirName), "%s/plugins", instDir);

    if (!(dir = opendir(dirName))) {
	int eno = errno;
	PSID_fwarn(eno, "opendir(%s) failed", dirName);
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

static void sendAvail(PStask_ID_t dest)
{
    if (PSIDnodes_getProtoV(PSC_getID(dest)) < 347) return sendAvailOld(dest);

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_CD_PLUGINRES, PSP_PLUGIN_AVAIL);
    setFragDest(&msg, dest);

    char *instDir = getenv("PSID_PLUGIN_PATH");
    if (!instDir) instDir = PSC_lookupInstalldir(NULL);
    if (!instDir) {
	PSID_flog("installation directory not found\n");
	addStringToMsg("installation directory not found\n", &msg);
	goto end;
    }

    char dirName[PATH_MAX];
    snprintf(dirName, sizeof(dirName), "%s/plugins", instDir);

    DIR *dir = opendir(dirName);
    if (!dir) {
	int eno = errno;
	PSID_fwarn(eno, "opendir(%s) failed", dirName);
	char res[1024];
	snprintf(res, sizeof(res), "opendir(%.512s) failed: %s\n", dirName,
		 strerror(eno));
	addStringToMsg(res, &msg);
	goto end;
    }

    rewinddir(dir);
    struct dirent *dent;
    while ((dent = readdir(dir))) {
	char *nameStr = dent->d_name;
	size_t nameLen = PSP_strLen(nameStr);

	if (nameLen && !strcmp(&nameStr[nameLen - 4], ".so")) {
	    nameStr[nameLen - 4] = '\0';
	    addStringToMsg(nameStr, &msg);
	}
    }
    closedir(dir);

end:
    /* Add stop entry and send*/
    addStringToMsg("", &msg);
    if (sendFragMsg(&msg) == -1) PSID_flog("sendFragMsg() failed\n");
}

static void sendHelpOld(PStask_ID_t dest, char *buf)
{
    char *pName = buf, *key = pName + PSP_strLen(pName);
    if (!*key) key = NULL;

    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGINRES,
	    .dest = dest,
	    .sender = PSC_getMyTID(),
	    .len = DDTypedBufMsgOffset },
	.type = PSP_PLUGIN_HELP };

    PSIDplugin_t plugin = PSIDplugin_find(pName);
    if (!plugin) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: unknown plugin '%.512s'\n",
		 __func__, pName);
	sendStr(&msg, mBuf, __func__);
    } else if (!plugin->help) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: no help-method for '%.512s' \n",
		 __func__, pName);
	sendStr(&msg, mBuf, __func__);
    } else {
	char *res = plugin->help(key);

	if (res) {
	    sendStr(&msg, res, __func__);
	    free(res);
	}
    }

    /* Create stop message */
    sendStr(&msg, "", __func__);
}

static void sendHelp(PStask_ID_t dest, char *buf)
{
    if (PSIDnodes_getProtoV(PSC_getID(dest)) < 347) return sendHelpOld(dest, buf);

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_CD_PLUGINRES, PSP_PLUGIN_HELP);
    setFragDest(&msg, dest);

    char *pName = buf;
    PSIDplugin_t plugin = PSIDplugin_find(pName);
    if (!plugin) {
	char res[1024];
	snprintf(res, sizeof(res), "\tpsid: %s: unknown plugin '%.512s'\n",
		 __func__, pName);
	addStringToMsg(res, &msg);
    } else if (!plugin->help) {
	char res[1024];
	snprintf(res, sizeof(res), "\tpsid: %s: no help-method for '%.512s' \n",
		 __func__, pName);
	addStringToMsg(res, &msg);
    } else {
	char *key = pName + PSP_strLen(pName);
	char *res = plugin->help(*key ? key : NULL);
	addStringToMsg(res ? res : "", &msg);
	free(res);
    }

    if (sendFragMsg(&msg) == -1) PSID_flog("sendFragMsg() failed\n");
}

static void handleSetKeyOld(PStask_ID_t dest, char *buf)
{
    char *pName = buf, *key = pName + PSP_strLen(pName);
    char *val = key + PSP_strLen(key);
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGINRES,
	    .dest = dest,
	    .sender = PSC_getMyTID(),
	    .len = DDTypedBufMsgOffset },
	.type = PSP_PLUGIN_SET };

    PSIDplugin_t plugin = PSIDplugin_find(pName);
    if (!plugin) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: unknown plugin '%.512s'\n",
		 __func__, pName);
	sendStr(&msg, mBuf, __func__);
    } else if (!plugin->set) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: no set-method for '%.512s' \n",
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

static void handleSetKey(PStask_ID_t dest, char *buf)
{
    if (PSIDnodes_getProtoV(PSC_getID(dest)) < 347) return handleSetKeyOld(dest, buf);

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_CD_PLUGINRES, PSP_PLUGIN_SET);
    setFragDest(&msg, dest);

    char *pName = buf;
    PSIDplugin_t plugin = PSIDplugin_find(pName);
    if (!plugin) {
	char res[1024];
	snprintf(res, sizeof(res), "\tpsid: %s: unknown plugin '%.512s'\n",
		 __func__, pName);
	addStringToMsg(res, &msg);
    } else if (!plugin->set) {
	char res[1024];
	snprintf(res, sizeof(res), "\tpsid: %s: no set-method for '%.512s' \n",
		 __func__, pName);
	addStringToMsg(res, &msg);
    } else {
	char *key = pName + PSP_strLen(pName), *val = key + PSP_strLen(key);
	char *res = plugin->set(key, val);
	addStringToMsg(res ? res : "", &msg);
	free(res);
    }

    if (sendFragMsg(&msg) == -1) PSID_flog("sendFragMsg() failed\n");
}

static void handleUnsetKeyOld(PStask_ID_t dest, char *buf)
{
    char *pName = buf, *key = pName + PSP_strLen(pName);
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGINRES,
	    .dest = dest,
	    .sender = PSC_getMyTID(),
	    .len = DDTypedBufMsgOffset },
	.type = PSP_PLUGIN_UNSET };

    PSIDplugin_t plugin = PSIDplugin_find(pName);
    if (!plugin) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: unknown plugin '%.512s'\n",
		 __func__, pName);
	sendStr(&msg, mBuf, __func__);
    } else if (!plugin->unset) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: no unset-method for '%.512s' \n",
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

static void handleUnsetKey(PStask_ID_t dest, char *buf)
{
    if (PSIDnodes_getProtoV(PSC_getID(dest)) < 347) return handleUnsetKeyOld(dest, buf);

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_CD_PLUGINRES, PSP_PLUGIN_UNSET);
    setFragDest(&msg, dest);

    char *pName = buf;
    PSIDplugin_t plugin = PSIDplugin_find(pName);
    if (!plugin) {
	char res[1024];
	snprintf(res, sizeof(res), "\tpsid: %s: unknown plugin '%.512s'\n",
		 __func__, pName);
	addStringToMsg(res, &msg);
    } else if (!plugin->unset) {
	char res[1024];
	snprintf(res, sizeof(res), "\tpsid: %s: no unset-method for '%.512s' \n",
		 __func__, pName);
	addStringToMsg(res, &msg);
    } else {
	char *key = pName + PSP_strLen(pName);
	char *res = plugin->unset(key);
	addStringToMsg(res ? res : "", &msg);
	free(res);
    }

    if (sendFragMsg(&msg) == -1) PSID_flog("sendFragMsg() failed\n");
}

static void handleShowKeyOld(PStask_ID_t dest, char *buf)
{
    char *pName = buf, *key = pName + PSP_strLen(pName);
    if (!*key) key = NULL;

    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGINRES,
	    .dest = dest,
	    .sender = PSC_getMyTID(),
	    .len = DDTypedBufMsgOffset },
	.type = PSP_PLUGIN_SHOW };

    PSIDplugin_t plugin = PSIDplugin_find(pName);
    if (!plugin) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: unknown plugin '%.512s'\n",
		 __func__, pName);
	sendStr(&msg, mBuf, __func__);
    } else if (!plugin->show) {
	char mBuf[sizeof(msg.buf)];
	snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: no show-method for '%.512s' \n",
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

static void handleShowKey(PStask_ID_t dest, char *buf)
{
    if (PSIDnodes_getProtoV(PSC_getID(dest)) < 347) return handleShowKeyOld(dest, buf);

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_CD_PLUGINRES, PSP_PLUGIN_SHOW);
    setFragDest(&msg, dest);

    char *pName = buf;
    PSIDplugin_t plugin = PSIDplugin_find(pName);
    if (!plugin) {
	char res[1024];
	snprintf(res, sizeof(res), "\tpsid: %s: unknown plugin '%.512s'\n",
		 __func__, pName);
	addStringToMsg(res, &msg);
    } else if (!plugin->show) {
	char res[1024];
	snprintf(res, sizeof(res), "\tpsid: %s: no show-method for '%.512s' \n",
		 __func__, pName);
	addStringToMsg(res, &msg);
    } else {
	char *key = pName + PSP_strLen(pName);
	char *res = plugin->show(*key ? key : NULL);
	addStringToMsg(res ? res : "", &msg);
	free(res);
    }

    if (sendFragMsg(&msg) == -1) PSID_flog("sendFragMsg() failed\n");
}

static void sendLoadTime(PStask_ID_t dest, PSIDplugin_t plugin)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGINRES,
	    .dest = dest,
	    .sender = PSC_getMyTID(),
	    .len = DDTypedBufMsgOffset },
	.type = PSP_PLUGIN_LOADTIME };
    char mBuf[sizeof(msg.buf)];

    if (!plugin) return;

    snprintf(mBuf, sizeof(mBuf), "\t%10s %4d %s", plugin->name, plugin->version,
	     ctime(&plugin->load.tv_sec));
    sendStr(&msg, mBuf, __func__);
}

static void handleLoadTimeOld(PStask_ID_t dest, char *pName)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGINRES,
	    .dest = dest,
	    .sender = PSC_getMyTID(),
	    .len = DDTypedBufMsgOffset },
	.type = PSP_PLUGIN_LOADTIME };

    if (!pName) return;

    if (*pName) {
	PSIDplugin_t plugin = PSIDplugin_find(pName);
	if (!plugin) {
	    char mBuf[sizeof(msg.buf)];
	    snprintf(mBuf, sizeof(mBuf), "\tpsid: %s: plugin '%.512s' not found\n",
		     __func__, pName);
	    sendStr(&msg, mBuf, __func__);
	    msg.header.len = DDTypedBufMsgOffset;
	} else {
	    sendLoadTime(dest, plugin);
	}
    } else {
	list_t *p;
	list_for_each(p, &pluginList) {
	    PSIDplugin_t plugin = list_entry(p, struct PSIDplugin, next);

	    sendLoadTime(dest, plugin);
	}
    }

    /* Create stop message */
    sendStr(&msg, "", __func__);

    return;
}

static void addLoadTimeToMsg(PSIDplugin_t plugin, PS_SendDB_t *buf)
{
    if (!plugin) return;

    char res[128];
    snprintf(res, sizeof(res), "\t%10s %4d %s", plugin->name, plugin->version,
	     ctime(&plugin->load.tv_sec));
    addStringToMsg(res, buf);
}

static void handleLoadTime(PStask_ID_t dest, char *pName)
{
    if (!pName) return;

    if (PSIDnodes_getProtoV(PSC_getID(dest)) < 347) return handleLoadTimeOld(dest, pName);

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_CD_PLUGINRES, PSP_PLUGIN_LOADTIME);
    setFragDest(&msg, dest);

    if (*pName) {
	PSIDplugin_t plugin = PSIDplugin_find(pName);
	if (!plugin) {
	    char res[1024];
	    snprintf(res, sizeof(res), "\tpsid: %s: plugin '%.512s' not found\n",
		     __func__, pName);
	    addStringToMsg(res, &msg);
	} else {
	    addLoadTimeToMsg(plugin, &msg);
	}
    } else {
	list_t *p;
	list_for_each(p, &pluginList) {
	    PSIDplugin_t plugin = list_entry(p, struct PSIDplugin, next);
	    addLoadTimeToMsg(plugin, &msg);
	}
    }

    /* Add stop entry */
    addStringToMsg("", &msg);
    if (sendFragMsg(&msg) == -1) PSID_flog("sendFragMsg() failed\n");
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
    PSID_fdbg(PSID_LOG_PLUGIN, "(%s, %s)\n",
	      PSC_printTID(inmsg->header.sender), inmsg->buf);

    int res = 0;
    if (!PSID_checkPrivilege(inmsg->header.sender)) {
	switch (inmsg->type) {
	case PSP_PLUGIN_AVAIL:
	case PSP_PLUGIN_HELP:
	case PSP_PLUGIN_SHOW:
	case PSP_PLUGIN_LOADTIME:
	    break;
	default:
	    PSID_flog("task %s not allowed to touch plugins\n",
		      PSC_printTID(inmsg->header.sender));
	    res = EACCES;
	    goto end;
	}
    }

    PSnodes_ID_t destID = PSC_getID(inmsg->header.dest);
    if (destID != PSC_getMyID()) {
	if (!PSIDnodes_isUp(destID)) {
	    res = EHOSTDOWN;
	    goto end;
	}
	if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	    res = errno;
	    PSID_fwarn(res, "sendMsg()");
	    goto end;
	}
	return true; /* destination node will send PLUGINRES message */
    }

    switch (inmsg->type) {
    case PSP_PLUGIN_LOAD:
	if (!PSIDplugin_load(inmsg->buf, 0, NULL, pluginLogfile)) res = -1;
	break;
    case PSP_PLUGIN_REMOVE:
	if (PSIDplugin_finalize(inmsg->buf) < 0) res = ENODEV;
	break;
    case PSP_PLUGIN_FORCEREMOVE:
	if (forceUnloadPlugin(inmsg->buf) < 0) res = ENODEV;
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
	PSID_flog("unknown message type %d\n", inmsg->type);
	res = ENOTSUP;
    }

end:
    /* prepare answer depending on inmsg->type */
    switch(inmsg->type) {
    case PSP_PLUGIN_LOAD:
    case PSP_PLUGIN_REMOVE:
    case PSP_PLUGIN_FORCEREMOVE:
	/* msg.type expected to contain error flag */
	;
	DDTypedBufferMsg_t msg = {
	    .header = {
		.type = PSP_CD_PLUGINRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = DDTypedBufMsgOffset },
	    .type = res,
	    .buf[0] = '\0', };
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg()");
	}
	break;
    default:
	if (PSIDnodes_getProtoV(PSC_getID(msg.header.dest)) < 347) {
	    DDTypedBufferMsg_t msg = {
		.header = {
		    .type = PSP_CD_PLUGINRES,
		    .dest = inmsg->header.sender,
		    .sender = PSC_getMyTID(),
		    .len = DDTypedBufMsgOffset },
		.type = inmsg->type,
		.buf[0] = '\0', };
	    /* msg.type expected to contain original type / error in buf */
	    if (res) {
		char tmp[1024];
		snprintf(tmp, sizeof(tmp), "\tpsid: %.512s\n", strerror(res));
		sendStr(&msg, tmp, __func__);
	    }
	    sendStr(&msg, "", __func__);
	} else {
	    PS_SendDB_t msg;
	    initFragBuffer(&msg, PSP_CD_PLUGINRES, PSP_PLUGIN_DROPPED);
	    setFragDest(&msg, inmsg->header.sender);

	    addInt32ToMsg(res, &msg);
	    addInt32ToMsg(inmsg->type, &msg);
	    if (sendFragMsg(&msg) == -1) PSID_flog("sendFragMsg() failed\n");
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
    DDTypedMsg_t typmsg = {
	.header = {
	    .type = PSP_CD_PLUGINRES,
	    .dest = msg->header.sender,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(typmsg) },
	.type = -1 };

    sendMsg(&typmsg);
    return true;
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
    struct timeval now;
    gettimeofday(&now, NULL);

    list_t *p, *tmp;
    list_for_each_safe(p, tmp, &pluginList) {
	PSIDplugin_t plugin = list_entry(p, struct PSIDplugin, next);
	if (plugin->finalized && !plugin->unload
	    && timerisset(&plugin->grace) && timercmp(&now, &plugin->grace, >)) {
	    PSID_fdbg(PSID_LOG_PLUGIN, "finalize() timed out for %s\n",
		      plugin->name);

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
	    PSID_flog("no name given\n");
	    free(ent);
	    continue;
	}

	PSID_fdbg(PSID_LOG_PLUGIN, "load '%s'\n", ent->name);
	if (!PSIDplugin_load(ent->name, 0, NULL, logfile)) {
	    PSID_flog("loading '%s' failed\n", ent->name);
	}
	free(ent->name);
	free(ent);
    }

    /* Store logfile for plugins loaded during runtime, too */
    pluginLogfile = logfile;
}
