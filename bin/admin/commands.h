/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Commands of the ParaStation Administration Tool
 */
#ifndef __COMMANDS_H
#define __COMMANDS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "pstask.h"
#include "psprotocol.h"
#include "pspartition.h"

/**
 * @brief Signal handler
 *
 * The central signal handler of the ParaStation administration tool
 * psiadmin.
 *
 * This one is put into commands, since the shutdown during reset of
 * the local ParaStation daemon has to get prevented.
 *
 * @param signal The signal to handle
 *
 * @return No return value
 */
void PSIADM_sighandler(int signal);

/**
 * @brief Add nodes
 *
 * Add the nodes marked within the node list @a nl to the
 * cluster. Therefore the local ParaStation daemon is requested to
 * start the corresponding daemons on the remote nodes, if this did
 * not already happen.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_AddNode(bool *nl);

/**
 * @brief Shutdown nodes
 *
 * Remove the nodes marked within the node list @a nl from the
 * cluster. Therefore, the local ParaStation daemon is requested to
 * shutdown the corresponding daemon on the remote nodes, if this did
 * not already happen. Only the daemons (and processes controlled by
 * them) are stopped, not the whole machine.
 *
 * If the local daemon is instructed to shutdown, too, the requesting
 * process will also be killed.
 *
 * If the @a silent flag is 0, a warning is printed for each node
 * marked in @a nl that is already down. Otherwise, such messages will
 * be suppressed.
 *
 * @param silent Suppress messages on nodes already down
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_ShutdownNode(int silent, bool *nl);

/**
 * @brief Start communication hardware
 *
 * Start the communication hardware @a hw on the nodes marked within
 * the node list @a nl.
 *
 * @param hw Communication hardware to start
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_HWStart(int32_t hw, bool *nl);

/**
 * @brief Stop communication hardware
 *
 * Stop the communication hardware @a hw on the nodes marked within
 * the node list @a nl.
 *
 * @param hw Communication hardware to stop
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_HWStop(int32_t hw, bool *nl);

/**
 * @brief Show node status
 *
 * Show the status of the nodes marked within the node list @a
 * nl. Therefore the corresponding list containing the information is
 * requested from the local daemon and printed to stdout.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_NodeStat(bool *nl);

/**
 * @brief Show node status summary
 *
 * Show the status summary of the nodes marked within the node list @a
 * nl. Therefore the corresponding list containing the information is
 * requested from the local daemon and printed to stdout in a
 * compressed fashion only giving the number of up and down nodes. In
 * the special case where less than @a max nodes are down also the IDs
 * of the down nodes are printed in a additional line.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @param max Maximum number of down nodes displayed in a separate
 * list
 *
 * @return No return value
 */
void PSIADM_SummaryStat(bool *nl, int max);

/**
 * @brief Show node's start-time
 *
 * Show the ParaStation daemon's start-time of the nodes marked within
 * the node list @a nl.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_StarttimeStat(bool *nl);

/**
 * @brief Show node's script info
 *
 * Show the ParaStation daemon's scripts of the nodes marked within
 * the node list @a nl. Depending on the @a type argument, different
 * information is listed:
 *
 * - PSP_INFO_STARTUPSCRIPT request the name of the script run during
 * startup of the daemon in order to test local features and stati.
 * This information is used to decide, if the local node is capable to
 * take part in the cluster-action.
 *
 * - PSP_INFO_NODEUPSCRIPT request the name of the script called by
 * the master daemon whenever a node becomes active in the concert of
 * daemons within a cluster.
 *
 * - PSP_INFO_NODEDOWNSCRIPT request the name of the script called by
 * the master daemon whenever a node disappears from the concert of
 * daemons within a cluster.
 *
 * @param type The type of scripts to display
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_ScriptStat(PSP_Info_t type, bool *nl);

/**
 * @brief Show some nodes
 *
 * Show the nodes in the node list @a nl that are marked to
 * be up or down. The type of nodes to show is selected via @a
 * mode. The nodes are listed as a newline separated list of host names
 * enabling the output to serve as an input within shell-scripts.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @param mode If 'u', up nodes are printed. If 'd', down node are
 * printed. Otherwise some error message is generated and no further
 * actions applies.
 *
 * @return No return value
 */
void PSIADM_SomeStat(bool *nl, char mode);

/**
 * @brief Show RDP status
 *
 * Show the RDP status on the nodes marked within the node list @a
 * nl. Therefore the corresponding list containing the information is
 * requested from the local daemon and printed to stdout.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_RDPStat(bool *nl);

/**
 * @brief Show RDP connection status
 *
 * Show the status of RDP connections on the nodes marked within the
 * node list @a nl. Therefore the corresponding list containing the
 * information is requested from the local daemon and printed to
 * stdout.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_RDPConnStat(bool *nl);

/**
 * @brief Show MCast status
 *
 * Show the MCast status on the nodes marked within the node list @a
 * nl. Therefore the corresponding list containing the information is
 * requested from the local daemon and printed to stdout.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_MCastStat(bool *nl);

/**
 * @brief Show Counter statuses
 *
 * Show the counter statuses connected to the communication hardware
 * @a hw of the nodes marked within the node list @a nl. Therefore the
 * corresponding list containing the information is requested from the
 * local daemon and printed to stdout.
 *
 * @param hw Communication hardware which counters should be displayed
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_CountStat(int hw, bool *nl);

/**
 * @brief Show process status
 *
 * Show the process status, i.e. the task ID, parent's task ID, user
 * ID and type of each process, of the nodes marked within the
 * node list @a nl. Therefore the corresponding lists containing the
 * information are requested from the local daemon and printed to
 * stdout.
 *
 * The maximum number of process information displayed per node is @a
 * count. If more than @a count processes per node exists, a hint
 * concerning the left out processes is displayed. If @a count is -1,
 * all processes are displayed.
 *
 * If @a full is different from 0, all processes (controlled by the
 * ParaStation daemon) are displayed. Otherwise only "normal"
 * processes, i.e. only processes not including forwarder, spawner or
 * monitor processes, are displayed.
 *
 * @param count The Maximum number of tasks to be displayed
 *
 * @param full Flag to mark if all jobs or only "normal" jobs shall be
 * displayed
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_ProcStat(int count, bool full, bool *nl);

/**
 * @brief Get load status
 *
 * Show the load status of the nodes marked within the node list @a
 * nl. Therefore the corresponding list containing the information is
 * requested from the local daemon and printed to stdout.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_LoadStat(bool *nl);

/**
 * @brief Get memory status
 *
 * Show the memory status of the nodes marked within the node list @a
 * nl. Therefore the corresponding list containing the information is
 * requested from the local daemon and printed to stdout.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_MemStat(bool *nl);

/**
 * @brief Get hardware status
 *
 * Show the hardware status of the nodes marked within the node list
 * @a nl. Therefore the corresponding list containing the information
 * is requested from the local daemon and printed to stdout.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_HWStat(bool *nl);

/**
 * @brief Get install dir
 *
 * Show the installation directory of the nodes marked within the
 * node list @a nl. Therefore the corresponding list containing the
 * information is requested from the corresponding daemon and printed
 * to stdout.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_InstdirStat(bool *nl);

/**
 * @brief Show jobs
 *
 * Show the jobs registered to the master daemon. This only includes
 * "normal" jobs, i.e. jobs not accounted and, thus, not registered to
 * the master daemon are not displayed.
 *
 * Therefore the corresponding list containing the information is
 * requested from the master daemon and printed to stdout.
 *
 * Via the @a opt parameter the kind of information to be presented
 * might be restricted.
 *
 * Only information concerning a special jobs might be requested by
 * setting the @a task parameter to a valid task ID. Then the
 * corresponding job is determined and the requested information is
 * requested and displayed.
 *
 * @param task A task ID of one process within a job. If set to 0
 * information on all task (as defined via @a opt) is provided.
 *
 * @param opt Set of flags describing the kind of information to be
 * retrieved
 *
 * @return No return value
 */
void PSIADM_JobStat(PStask_ID_t task, PSpart_list_t opt);

/**
 * @brief Get daemon versions
 *
 * Show the daemon versions of the nodes marked within the node list
 * @a nl. Therefore the corresponding information is requested from
 * the daemons and printed to stdout. Two version numbers are
 * presented, the current revisions of psid.c and the revision of the
 * RPM-file the daemon is installed from.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_VersionStat(bool *nl);

/**
 * @brief Show plugins
 *
 * Show the plugins loaded by the daemons of the nodes marked within
 * the node list @a nl. Therefore the corresponding information is
 * requested from the daemons and printed to stdout.
 *
 * For each plugin a line is printed containing the plugin's name, its
 * version and a list of plugins depending on this plugin.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_PluginStat(bool *nl);

/**
 * @brief Show environment
 *
 * Show the environment of the daemon processes of the nodes marked
 * within the node list @a nl. Therefore the corresponding information
 * is requested from the daemons and printed to stdout.
 *
 * For each environment variable a line is printed containing the
 * variables name and its value.
 *
 * @param key Name of the environment variable to display. If NULL,
 * all variables are displayed.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_EnvStat(char *key, bool *nl);

/**
 * @brief Set parameter
 *
 * Set the parameter @a type to the value @a value on the nodes marked
 * within the node list @a nl.
 *
 * @param type Parameter type to set
 *
 * @param value Parameter value to set
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_SetParam(PSP_Option_t type, PSP_Optval_t value, bool *nl);

/**
 * Structure for option-values to be send in a series of messages.
 */
typedef struct {
    size_t num;           /**< The number of valid entries in @a value */
    PSP_Optval_t *value;  /**< The actual option-values to be sent */
} PSIADM_valList_t;

/**
 * @brief Set list of parameters
 *
 * Set various parameters of type @a type to the values stored within
 * @a val on the nodes marked within the node list @a nl. The
 * parameters are send in a series of as few messages of type @ref
 * PSP_CD_SETOPTION as possible.
 *
 * This is mainly used to set and modify the cpumaps.
 *
 * @param type Parameter type to set
 *
 * @param val Parameter values to set
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_SetParamList(PSP_Option_t type, PSIADM_valList_t *val, bool *nl);

/**
 * @brief Show parameter
 *
 * Show the parameter @a type on the nodes marked within the node list
 * @a nl. Therefore the corresponding list containing the information
 * is requested from the local daemon and printed to stdout.
 *
 * @param type Parameter type requested
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_ShowParam(PSP_Option_t type, bool *nl);

/**
 * @brief Show parameter list
 *
 * Show the list of parameters of type @a type on the nodes marked
 * within the node list @a nl. Therefore the corresponding list
 * containing the information is requested from the local daemon and
 * printed to stdout.
 *
 * @param type Parameter type requested
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_ShowParamList(PSP_Option_t type, bool *nl);

/**
 * @brief Reset nodes
 *
 * Reset the nodes marked within the node list @a nl. If @a reset_hw is
 * different from 0, all communication hardware on that nodes is
 * reset, too. Otherwise only all controlled processes are killed and
 * the daemon is reset into a defined state.
 *
 * @param reset_hw Flag to mark communication hardware to be reset
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_Reset(int reset_hw, bool *nl);

/**
 * @brief Test network
 *
 * The the network by using the 'test_nodes' program on all nodes of
 * the cluster. A detailed description of the test program may be
 * found within the corresponding manual page.
 *
 * @param mode Historical parameter, currently ignored
 *
 * @return No return value
 *
 * @see test_nodes(1)
 */
void PSIADM_TestNetwork(int mode);

/**
 * @brief Kill process
 *
 * Kill the process denoted by the unique task ID @a tid by sending
 * the signal @a sig via the local and remote ParaStation daemons.
 *
 * @param tid The task ID of the process to signal
 *
 * @param sig The signal to send
 *
 * @return No return value
 */
void PSIADM_KillProc(PStask_ID_t tid, int sig);

/**
 * @brief Resolve nodes
 *
 * Resolve the nodes marked within the node list @a nl. For each node a
 * line giving the node ID and the host name is printed.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @return No return value
 */
void PSIADM_Resolve(bool *nl);

/**
 * @brief Handle plugins
 *
 * Handle the plugin @a name on the nodes selected within the node list
 * @a nl. Depending on the @a action, the plugin is loaded
 * (PSP_PLUGIN_LOAD) or unloaded (PSP_PLUGIN_REMOVE).
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @param name Name of the plugin to handle
 *
 * @param action The action to apply on the requested plugin
 *
 * @return No return value
 */
void PSIADM_Plugin(bool *nl, char *name, PSP_Plugin_t action);

/**
 * @brief Handle plugin settings
 *
 * Handle the settings of the plugin @a name on the nodes selected
 * within the node list @a nl. Depending on the @a action, the setting
 * indexed by @a key might be set to @a value (PSP_PLUGIN_SET), unset
 * (PSP_PLUGIN_UNSET) or displayed to stdout (PSP_PLUGIN_SHOW).
 *
 * If actions is PSP_PLUGIN_SHOW, @a key might be NULL in order to
 * display all settings of the given plugin.
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @param name Name of the plugin to handle
 *
 * @param key Index used to identify the setting to handle
 *
 * @param value Strings holding the actual data to be set (in case of
 * PSP_PLUGIN_SET)
 *
 * @param action The action to apply on the requested plugin
 *
 * @return No return value
 */
void PSIADM_PluginKey(bool *nl, char *name, char *key, char *value,
		      PSP_Plugin_t action);

/**
 * @brief Modify environment
 *
 * Modify the environment variable @a key on the nodes selected within
 * the node list @a nl. Depending on the @a action, the variable is set
 * to @a value (PSP_ENV_SET) or unset (PSP_ENV_UNSET).
 *
 * @param nl Array indexed by node ID flagging nodes to take action on
 *
 * @param key Name of the environment variable to modify
 *
 * @param value Value to be set if @a action is PSP_ENV_SET
 *
 * @param action The action to apply on the requested environment
 * variable
 *
 * @return No return value
 */
void PSIADM_Environment(bool *nl, char *key, char *value, PSP_Env_t action);

#endif /* __COMMANDS_H */
