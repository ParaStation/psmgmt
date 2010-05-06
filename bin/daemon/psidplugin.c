/*
 *               ParaStation
 *
 * Copyright (C) 2009-2010 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <dlfcn.h>

#include "list.h"
#include "plugin.h"
#include "psprotocol.h"
#include "pscommon.h"
#include "config_parsing.h"
#include "psidutil.h"
#include "psidcomm.h"
#include "psidtask.h"
#include "psidnodes.h"

#include "psidplugin.h"

/** Structure holding all information concerning a plugin */
typedef struct {
    list_t next;       /**< Used to put into @ref pluginList */
    list_t triggers;   /**< List of plugins triggering this one */
    list_t depends;    /**< List of plugins this one depends on */
    void *handle;      /**< Handle created by dlopen() */
    char *name;        /**< Actual name */
    int version;       /**< Actual version */
} plugin_t;

/**
 * Structure used to create @a trigger and @a depends members of @ref
 * plugin_t
 */
typedef struct {
    plugin_t * plugin; /**< Corresponding plugin added to the list */
    list_t next;       /**< Actual list entry */
} plugin_ref_t;

/** List of currently loaded plugins */
static LIST_HEAD(pluginList);

/** API version currently implemented */
static int pluginAPIVersion = 100;

static int unloadPlugin(plugin_t * plugin);

/**
 * @brief Find trigger
 *
 * Find the triggering plugin @a trigger in the list of triggers of
 * plugin @a plugin.
 *
 * @param plugin The plugin to search in
 *
 * @param trigger The triggering plugin to search for
 *
 * @return Return the reference to the plugin trigger, if it is found
 * within the list. Otherwise NULL is given back.
 */
static plugin_ref_t * findTrigger(plugin_t *plugin, plugin_t *trigger)
{
    list_t *t;

    if (!plugin || !trigger) return NULL;

    list_for_each(t, &plugin->triggers) {
	plugin_ref_t *ref = list_entry(t, plugin_ref_t, next);

	if (ref->plugin == trigger) return ref;
    }

    return NULL;
}

/**
 * @brief Add trigger
 *
 * Add the triggering plugin @a trigger to the list of triggers of
 * plugin @a plugin.
 *
 * @param plugin The plugin that was triggered
 *
 * @parem trigger The triggering plugin
 *
 * @return If a trigger was added, 1 is returned. In case the trigger
 * was already registered, 0 is given back. Or -1 in case of an error.
 */
static int addTrigger(plugin_t *plugin, plugin_t *trigger)
{
    plugin_ref_t *new;

    if (findTrigger(plugin, trigger)) return 0;

    new = malloc(sizeof(*new));
    if (!new) {
	PSID_warn(-1, errno, "%s", __func__);
	return -1;
    }

    new->plugin = trigger;
    list_add_tail(&new->next, &plugin->triggers);

    return 1;
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
    plugin_ref_t *ref;

    if (!plugin || !trigger) return NULL;

    ref = findTrigger(plugin, trigger);
    if (!ref) {
	PSID_log(plugin == trigger ? PSID_LOG_PLUGIN : -1,
		 "%s: trigger '%s' not found in '%s'\n",
		 __func__, trigger->name, plugin->name);
	return NULL;
    }

    list_del(&ref->next);
    free(ref);

    if (list_empty(&plugin->triggers)) unloadPlugin(plugin);

    return trigger;
}

/**
 * @brief Create trigger list
 *
 * Create a string describing the list of triggering plugins @a
 * triggerlist. The character-strings is written into @a buf. At most
 * @a size characters are written into buf.
 *
 * If @a triggerList would require more than @a size characters, the
 * describing string will be chopped.
 *
 * @param buf A buffer holding the created string upon return
 *
 * @param size Number of characters @a buf is able to hold
 *
 * @param triggerList The list of triggering plugins to be displayed
 *
 * @return No return value.
 */
void printTriggerList(char *buf, size_t size, list_t *triggerList)
{
    list_t *t;

    list_for_each(t, triggerList) {
	plugin_ref_t *ref = list_entry(t, plugin_ref_t, next);

	if (ref && ref->plugin && ref->plugin->name
	    && &ref->plugin->triggers != triggerList)
	    snprintf(buf+strlen(buf), size-strlen(buf),
		     " %s", ref->plugin->name);
    }
}

/**
 * @brief Add dependant
 *
 * Add the depending plugin @a depend to the list of dependants of
 * plugin @a plugin.
 *
 * @param plugin The plugin that is a dependant
 *
 * @parem depend The depending plugin
 *
 * @return If a trigger was added, 1 is returned. Or -1 in case of an
 * error.
 */
static int addDepend(plugin_t *plugin, plugin_t *depend)
{
    plugin_ref_t *new;

    new = malloc(sizeof(*new));
    if (!new) {
	PSID_warn(-1, errno, "%s", __func__);
	return -1;
    }

    new->plugin = depend;
    list_add_tail(&new->next, &plugin->depends);

    return 1;
}

/**
 * @brief Find plugin
 *
 * Find a plugin by its name @a name from the list of plugins @ref
 * pluginList.
 *
 * @param name Name of the plugin to find.
 *
 * @return If the plugin was found, a pointer to the describing
 * structure is returned. Or NULL otherwise.
 */
static plugin_t * findPlugin(char *name)
{
    list_t *p;

    if (!name || ! *name) return NULL;

    list_for_each(p, &pluginList) {
	plugin_t *plugin = list_entry(p, plugin_t, next);

	if (!strcmp(name, plugin->name)) return plugin;
    }

    return NULL;
}

/**
 * @brief Create plugin structure
 *
 * Create and initialize a plugin structure. The structure will
 * describe the loaded plugin referred by @a handle with name @a name
 * and version @a version.
 *
 * @param handle Handle of the plugin created via dlopen().
 *
 * @param name Name of the plugin.
 *
 * @param version Version of the plugin.
 *
 * @return Return the newly created structure, or NULL, if some error
 * occurred.
 */
static plugin_t * newPlugin(void *handle, char *name, int version)
{
    plugin_t *plugin;

    PSID_log(PSID_LOG_PLUGIN, "%s(%p,%s,%d)\n",__func__, handle, name, version);

    plugin = malloc(sizeof(*plugin));
    if (!plugin) {
	PSID_warn(-1, errno, "%s", __func__);
	return NULL;
    }

    plugin->name = strdup(name);
    plugin->handle = handle;
    plugin->version = version;
    INIT_LIST_HEAD(&plugin->next);
    INIT_LIST_HEAD(&plugin->triggers);
    INIT_LIST_HEAD(&plugin->depends);

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
 * @return No return valie.
 */
static void delPlugin(plugin_t *plugin)
{
    char line[80];

    if (!plugin) return;

    if (!list_empty(&plugin->triggers)) {
	printTriggerList(line, sizeof(line), &plugin->triggers);
	PSID_log(-1, "%s: '%s' still triggered by: %s\n",
		 __func__, plugin->name, line);
	return;
    }
    if (!list_empty(&plugin->depends)) {
	printTriggerList(line, sizeof(line), &plugin->depends);
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
 * @param new The plugin to register.
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

/**
 * @brief Load plugin
 *
 * Load the plugin @a name with minimum Version @a minVer. If loading
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
 * Loading a plugin might also fail due to loading dependant plugins
 * without success.
 *
 * If @a trigger is different from NULL, the corresponding plugin will
 * be marked as a triggering plugin within the newly created plugin.
 *
 * @param name Name of the plugin to load
 *
 * @param minVer Minimal required version of the plugin. Might be 0
 *
 * @param trigger Plugin triggering the current plugin to be loaded.
 *
 * @return Upon success, i.e. if the plugin is loaded afterwards, the
 * structure describing the plugin is given back. Or NULL, if the
 * plugin could not be loaded.
 */
static plugin_t * loadPlugin(char *name, int minVer, plugin_t * trigger)
{
    char filename[PATH_MAX], *instDir;
    void *handle = NULL;

    int *plugin_reqAPI = NULL;
    char *plugin_name = NULL;
    int *plugin_version = NULL;
    plugin_dep_t *plugin_deps = NULL;

    plugin_t *plugin = findPlugin(name);

    if (!name || ! *name) {
	PSID_log(-1, "%s: No name given\n", __func__);
	return NULL;
    }

    PSID_log(PSID_LOG_PLUGIN, "%s(%s, %d, %s)\n", __func__,
	     name, minVer, trigger ? trigger->name : "<no trigger>");

    if (plugin) {
	PSID_log(PSID_LOG_PLUGIN, "%s: version %d already loaded\n",
		 __func__, plugin->version);
	if (plugin->version < minVer) return NULL;

	if (addTrigger(plugin, trigger ? trigger : plugin) < 0) return NULL;

	return plugin;
    }

    instDir = PSC_lookupInstalldir(NULL);
    if (!instDir) {
	PSID_log(-1, "%s: installation directory not found\n", __func__);
	return NULL;
    }
    snprintf(filename, sizeof(filename), "%s/plugins/%s.so", instDir, name);
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
		 __func__, name, *plugin_reqAPI, pluginAPIVersion);
	dlclose(handle);
	return NULL;
    }

    if (plugin_name && *plugin_name) {
	if (strcmp(plugin_name, name)) {
	    PSID_log(-1, "%s: WARNING: plugin_name '%s' and name '%s' differ\n",
		     __func__, plugin_name, name);
	}
    }

    if (plugin_version) {
	PSID_log(PSID_LOG_PLUGIN, "%s: plugin_version is %d\n", __func__,
		 *plugin_version);
    }
    if (minVer) {
	if (!plugin_version) {
	    PSID_log(-1, "%s: Cannot determine version of '%s'\n",
		     __func__, name);
	    dlclose(handle);
	    return NULL;
	} else if (*plugin_version < minVer) {
	    PSID_log(-1,
		     "%s: 'version %d or above of '%s'required. This is %d\n",
		     __func__, minVer, name, *plugin_version);
	    dlclose(handle);
	    return NULL;
	}
    }

    plugin = newPlugin(handle, name, plugin_version ? *plugin_version : 100);

    if (plugin_deps) {
	plugin_dep_t *deps = plugin_deps;
	while (deps->name) {
	    plugin_t *d;
	    PSID_log(PSID_LOG_PLUGIN, "%s:   requires '%s' version %d\n",
		     __func__, deps->name, deps->version);
	    d = loadPlugin(deps->name, deps->version, plugin);
	    if (!d || addDepend(plugin, d) < 0) {
		unloadPlugin(plugin);
		dlclose(handle);
		return NULL;
	    }
	    deps++;
	}
    }

    if (addTrigger(plugin, trigger ? trigger : plugin) < 0)  {
	PSID_log(-1, "%s: addTrigger() failed\n", __func__);
	unloadPlugin(plugin);
	dlclose(handle);
	return NULL;
    }

    registerPlugin(plugin);

    return plugin;
}

/**
 * @brief Unload plugin
 *
 * Unload the plugin @a plugin. Unloading a plugin might fail due to
 * still existent dependcies from other plugins. This functions is
 * unable to force unloading a specific plugin.
 *
 * @param plugin The plugin to be unloaded
 *
 * @return Without error, i.e. if the plugin was successfully
 * unloaded, 1 is returned. Or 0, if some error occurred. In the
 * latter case the plugin might still be loaded.
 */
static int unloadPlugin(plugin_t *plugin)
{
    char line[80];
    list_t *d, *tmp;

    if (!plugin || !plugin->handle) return 0;

    PSID_log(PSID_LOG_PLUGIN, "%s(%s)\n", __func__, plugin->name);
    if (!list_empty(&plugin->triggers)) {
	/* Maybe the plugin is unloaded explicitely */
	remTrigger(plugin, plugin);
	if (!list_empty(&plugin->triggers)) {
	    printTriggerList(line, sizeof(line), &plugin->triggers);
	    PSID_log(PSID_LOG_PLUGIN, "%s: '%s' still triggered by: %s\n",
		     __func__, plugin->name, line);
	    return 0;
	}
	return 1;
    }

    /* Remove triggers from plugins we depend on */
    list_for_each_safe(d, tmp, &plugin->depends) {
	plugin_ref_t *ref = list_entry(d, plugin_ref_t, next);

	remTrigger(ref->plugin, plugin);
	list_del(&ref->next);
	free(ref);
    }

    if (dlclose(plugin->handle)) {
	PSID_log(-1, "%s: dlclose(%s): %s\n",
		 __func__, plugin->name, dlerror());
    } else {
	PSID_log(PSID_LOG_PLUGIN, "%s: '%s' successfully unloaded\n",
		 __func__, plugin->name);
    }
    plugin->handle = NULL;

    if (!list_empty(&plugin->next)) list_del(&plugin->next);
    delPlugin(plugin);

    return 1;
}


void PSID_sendPluginLists(PStask_ID_t dest)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_INFORESPONSE,
	    .sender = PSC_getMyTID(),
	    .dest = dest,
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = PSP_INFO_QUEUE_PLUGINS,
	.buf = {0}};

    list_t *p;

    list_for_each(p, &pluginList) {
	plugin_t *plugin = list_entry(p, plugin_t, next);
	size_t len;

	snprintf(msg.buf, sizeof(msg.buf), "%16s %3d  ",
		 plugin->name, plugin->version);
	if (!list_empty(&plugin->triggers)) {
	    printTriggerList(msg.buf + strlen(msg.buf),
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
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_PLUGIN(DDTypedBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(&managedTasks, inmsg->header.sender);
    int destID = PSC_getID(inmsg->header.dest), ret = 0;

    PSID_log(PSID_LOG_PLUGIN, "%s(%s, %s)\n", __func__,
	     PSC_printTID(inmsg->header.sender), inmsg->buf);

    if (PSC_getID(inmsg->header.sender) == PSC_getMyID()) {
	if (!task) {
	    PSID_log(-1, "%s: task %s not found\n",
		     __func__, PSC_printTID(inmsg->header.sender));
	    ret = EACCES;
	    goto end;
	} else if (task->uid) {
	    PSID_log(-1, "%s: Only root is allowed to load plugins\n",
		     __func__);
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
	return; /* destination node will send PLUGINRES message */
    } else {
	switch (inmsg->type) {
	case PSP_PLUGIN_LOAD:
	    if (!loadPlugin(inmsg->buf, 0, NULL)) {
		ret = -1;
		goto end;
	    }
	    break;
	case PSP_PLUGIN_REMOVE:
	{
	    plugin_t *plugin = findPlugin(inmsg->buf);

	    if (!plugin) {
		ret = ENODEV;
		goto end;
	    } else if (!unloadPlugin(plugin)) {
		ret = -1;
		goto end;
	    }
	    break;
	}
	default:
	    PSID_log(-1, "%s: Unknown message type %d\n", __func__,
		     inmsg->type);
	    ret = -1;
	    goto end;
	}
    }

end:
    {
	DDTypedMsg_t msg = (DDTypedMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_PLUGINRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = ret};
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	}
    }
}

void initPlugins(void)
{
    /* Register msg-handlers for plugin load/unload */
    PSID_registerMsg(PSP_CD_PLUGIN, (handlerFunc_t)msg_PLUGIN);
    PSID_registerMsg(PSP_CD_PLUGINRES, (handlerFunc_t)sendMsg);

    /* Handle list of plugins found in the configuration file */
    if (!list_empty(&config->plugins)) {
	list_t *p;

	list_for_each(p, &config->plugins) {
	    nameList_t *ent = list_entry(p, nameList_t, next);

	    if (!ent->name || ! *ent->name) {
		PSID_log(-1, "%s: Illegal name '%s'\n", __func__, ent->name);
		continue;
	    }

	    PSID_log(PSID_LOG_PLUGIN, "%s: load '%s'\n", __func__, ent->name);
	    if (!loadPlugin(ent->name, 0, NULL)) {
		PSID_log(-1, "%s: loading '%s' failed.\n", __func__, ent->name);
	    }
	}
    }
}
