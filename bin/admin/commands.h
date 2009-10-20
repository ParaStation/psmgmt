/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * Commands of the ParaStation Adminstration Tool
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __COMMANDS_H
#define __COMMANDS_H

#include "pstask.h"
#include "psprotocol.h"
#include "pspartition.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** The current CVS version string of the commands.c file. */
extern char commandsversion[];

/**
 * @brief Signal handler.
 *
 * The central signal handler of the ParaStation administration tool
 * psiadmin.
 *
 * This one is put into commands, since the shutdown during reset of
 * the local ParaStation daemon has to get prevented.
 *
 * @param signal The signal to handle.
 *
 * @return No return value.
 */
void PSIADM_sighandler(int signal);

/**
 * @brief Add nodes.
 *
 * Add the nodes marked within the nodelist @a nl to the
 * cluster. Therefore the local ParaStation daemon is requested to
 * start the corresponding daemons on the remote nodes, if this did
 * not already happen.
 *
 * @param nl The nodelist describing the nodes on which the action
 * should take effect.
 *
 * @return No return value.
 */
void PSIADM_AddNode(char *nl);

/**
 * @brief Shutdown nodes.
 *
 * Remove the nodes marked within the nodelist @a nl from the
 * cluster. Therefore the local ParaStation daemon is requested to
 * shutdown the corresponding daemons on the remote nodes, if this did
 * not already happen. Only the daemons (and processes controlled by
 * them) are stoped, not the whole machine.
 *
 * If also the local daemon is instructed to shutdown, the requesting
 * process will also be killed.
 *
 * @param nl The nodelist describing the nodes on which the action
 * should take effect.
 *
 * @return No return value.
 */
void PSIADM_ShutdownNode(char *nl);

/**
 * @brief Start communication hardware.
 *
 * Start the communication hardware @a hw on the nodes marked within
 * the nodelist @a nl.
 *
 * @param hw The communication hardware to start.
 *
 * @param nl The nodelist describing the nodes on which the action
 * should take effect.
 *
 * @return No return value.
 */
void PSIADM_HWStart(int hw, char *nl);

/**
 * @brief Stop communication hardware.
 *
 * Stop the communication hardware @a hw on the nodes marked within
 * the nodelist @a nl.
 *
 * @param hw The communication hardware to stop.
 *
 * @param nl The nodelist describing the nodes on which the action
 * should take effect.
 *
 * @return No return value.
 */
void PSIADM_HWStop(int hw, char *nl);

/**
 * @brief Show node status.
 *
 * Show the status of the nodes marked within the nodelist @a
 * nl. Therefore the corresponding list containing the information is
 * requested from the local daemon and printed to stdout.
 *
 * @param nl The nodelist describing the nodes from which the
 * information should requested and put out.
 *
 * @return No return value.
 */
void PSIADM_NodeStat(char *nl);

/**
 * @brief Show node status summary.
 *
 * Show the status summary of the nodes marked within the nodelist @a
 * nl. Therefore the corresponding list containing the information is
 * requested from the local daemon and printed to stdout in a
 * compressed fashion only giving the number of up and down nodes. In
 * the special case where less than @a max nodes are down also the IDs
 * of the down nodes are printed in a additional line.
 *
 * @param nl The nodelist describing the nodes from which the
 * information should requested and put out.
 *
 * @param max Maximum number of down nodes displayed in a separate
 * list.
 *
 * @return No return value.
 */
void PSIADM_SummaryStat(char *nl, int max);

/**
 * @brief Show node's start-time.
 *
 * Show the ParaStation daemon's start-time of the nodes marked within
 * the nodelist @a nl.
 *
 * @param nl The nodelist describing the nodes from which the
 * information should be requested and put out.
 *
 * @return No return value.
 */
void PSIADM_StarttimeStat(char *nl);

/**
 * @brief Show node's script info.
 *
 * Show the ParaStation daemon's scripts of the nodes marked within
 * the nodelist @a nl. Depending on the @a type argument, different
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
 * the master daemon whenever a node disappeares from the concert of
 * daemons within a cluster.
 *
 * @param type The type of scripts to display.
 *
 * @param nl The nodelist describing the nodes from which the
 * information should be requested and put out.
 *
 * @return No return value.
 */
void PSIADM_ScriptStat(PSP_Info_t type, char *nl);

/**
 * @brief Show some nodes.
 *
 * Show the nodes in the nodelist @a nl that are marked to
 * be up or down. The type of nodes to show is selected via @a
 * mode. The nodes are listed as a newline separated list of hostnames
 * enabling the output to serve as an input within shell-scripts.
 *
 * @param nl The nodelist describing the nodes from which the
 * information should requested and put out.
 *
 * @param mode If 'u', up nodes are printed. If 'd', down node are
 * printed. Otherwise some error message is generated and no further
 * actions applys.
 *
 * @return No return value.
 */
void PSIADM_SomeStat(char *nl, char mode);

/**
 * @brief Show RDP status.
 *
 * Show the RDP status on the nodes marked within the nodelist @a
 * nl. Therefore the corresponding list containing the information is
 * requested from the local daemon and printed to stdout.
 *
 * @param nl The nodelist describing the nodes from which the
 * information should requested and put out.
 *
 * @return No return value.
 */
void PSIADM_RDPStat(char *nl);

/**
 * @brief Show MCast status.
 *
 * Show the MCast status on the nodes marked within the nodelist @a
 * nl. Therefore the corresponding list containing the information is
 * requested from the local daemon and printed to stdout.
 *
 * @param nl The nodelist describing the nodes from which the
 * information should requested and put out.
 *
 * @return No return value.
 */
void PSIADM_MCastStat(char *nl);

/**
 * @brief Show Counter stati.
 *
 * Show the counter stati connected to the communication hardware @a
 * hw of the nodes marked within the nodelist @a nl. Therefore the
 * corresponding list containing the information is requested from the
 * local daemon and printed to stdout.
 *
 * @param hw The communication hardware which counters should be
 * displayed.
 *
 * @param nl The nodelist describing the nodes from which the
 * information should requested and put out.
 *
 * @return No return value.
 */
void PSIADM_CountStat(int hw, char *nl);

/**
 * @brief Show process status.
 *
 * Show the process status, i.e. the task ID, parent's task ID, user
 * ID and type of each process, of the nodes marked within the
 * nodelist @a nl. Therefore the corresponding lists containing the
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
 * @param count The maximum number of tasks to display.
 *
 * @param full Flag to mark, if all jobs or only "normal" jobs should
 * be displayed.
 *
 * @param nl The nodelist describing the nodes from which the
 * information should requested and put out.
 *
 * @return No return value.
 */
void PSIADM_ProcStat(int count, int full, char *nl);

/**
 * @brief Get load status.
 *
 * Show the load status of the nodes marked within the nodelist @a
 * nl. Therefore the corresponding list containing the information is
 * requested from the local daemon and printed to stdout.
 *
 * @param nl The nodelist describing the nodes from which the
 * information should requested and put out.
 *
 * @return No return value.
 */
void PSIADM_LoadStat(char *nl);

/**
 * @brief Get memory status.
 *
 * Show the memory status of the nodes marked within the nodelist @a
 * nl. Therefore the corresponding list containing the information is
 * requested from the local daemon and printed to stdout.
 *
 * @param nl The nodelist describing the nodes from which the
 * information should requested and put out.
 *
 * @return No return value.
 */
void PSIADM_MemStat(char *nl);

/**
 * @brief Get hardware status.
 *
 * Show the hardware status of the nodes marked within the nodelist @a
 * nl. Therefore the corresponding list containing the information is
 * requested from the local daemon and printed to stdout.
 *
 * @param nl The nodelist describing the nodes from which the
 * information should requested and put out.
 *
 * @return No return value.
 */
void PSIADM_HWStat(char *nl);

/**
 * @brief Show jobs.
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
 * Only informations concerning a special jobs might be requested by
 * setting the @a task parameter to a valid task ID. Then the
 * corresponding job is determined and the requested information is
 * requested and displayed.
 *
 *
 * @param task A task ID of one process within a job. If set to 0
 * information on all task (as defined via @a opt) is provided.
 *
 * @param opt Set of flags describing the kind of information to be
 * retrived.
 *
 * @return No return value.
 */
void PSIADM_JobStat(PStask_ID_t task, PSpart_list_t opt);

/**
 * @brief Get daemon versions.
 *
 * Show the daemon versions of the nodes marked within the nodelist @a
 * nl. Therefore the corresponding information is requested from the
 * daemons and printed to stdout. Two version numbers are presented,
 * the current revisions of psid.c and the revision of the RPM-file
 * the daemon is installed from.
 *
 * @param nl The nodelist describing the nodes from which the
 * information should requested and put out.
 *
 * @return No return value.
 */
void PSIADM_VersionStat(char *nl);

/**
 * @brief Show plugins.
 *
 * Show the plugins loaded by the daemons of the nodes marked within
 * the nodelist @a nl. Therefore the corresponding information is
 * requested from the daemons and printed to stdout.
 *
 * For each plugin a line is printed containing the plugin's name, its
 * version and a list of plugins depending on this plugin.
 *
 * @param nl The nodelist describing the nodes from which the
 * information should requested and put out.
 *
 * @return No return value.
 */
void PSIADM_PluginStat(char *nl);

/**
 * @brief Set parameter
 *
 * Set the parameter @a type to the value @a value on the nodes marked
 * within the nodelist @a nl.
 *
 * @param type The parameter type to set.
 *
 * @param value The parameter value to set.
 *
 * @param nl The nodelist describing the nodes on which the parameter
 * should be set.
 *
 * @return No return value.
 */
void PSIADM_SetParam(PSP_Option_t type, PSP_Optval_t value, char *nl);

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
 * @a val on the nodes marked within the nodelist @a nl. The
 * parameters are send in a series of as few messages of type @ref
 * PSP_CD_SETOPTION as possible.
 *
 * This is mainly used to set and modify the cpumaps.
 *
 * @param type The parameter type to set.
 *
 * @param val The parameter values to set.
 *
 * @param nl The nodelist describing the nodes on which the parameter
 * should be set.
 *
 * @return No return value.
 */
void PSIADM_SetParamList(PSP_Option_t type, PSIADM_valList_t *val, char *nl);

/**
 * @brief Show parameter
 *
 * Show the parameter @a type on the nodes marked within the nodelist
 * @a nl. Therefore the corresponding list containing the information
 * is requested from the local daemon and printed to stdout.
 *
 * @param type The parameter type requested.
 *
 * @param nl The nodelist describing the nodes from which the
 * parameter should be requested and put out.
 *
 * @return No return value.
 */
void PSIADM_ShowParam(PSP_Option_t type, char *nl);

/**
 * @brief Show parameter list
 *
 * Show the list of parameters of type @a type on the nodes marked
 * within the nodelist @a nl. Therefore the corresponding list
 * containing the information is requested from the local daemon and
 * printed to stdout.
 *
 * @param type The parameter type requested.
 *
 * @param nl The nodelist describing the nodes from which the
 * parameter should be requested and put out.
 *
 * @return No return value.
 */
void PSIADM_ShowParamList(PSP_Option_t type, char *nl);

/**
 * @brief Reset nodes.
 *
 * Reset the nodes marked within the nodelist @a nl. If @a reset_hw is
 * different from 0, all communication hardware on that nodes is
 * reset, too. Otherwise only all controlled processes are killed and
 * the daemon is reset into a defined state.
 *
 * @param reset_hw Flag to mark communication hardware to be reseted.
 *
 * @param nl The nodelist describing the node on which the action
 * should take effect.
 *
 * @return No return value.
 */
void PSIADM_Reset(int reset_hw, char *nl);

/**
 * @brief Test network
 *
 * The the network by using the 'test_nodes' program on all nodes of
 * the cluster. A detailed description of the test program may be
 * found within the corresponding manual page.
 *
 * @param mode Historical parameter, currently ignored.
 *
 * @return No return value.
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
 * @param tid The task ID of the process to signal.
 *
 * @param sig The signal to send.
 *
 * @return No return value.
 */
void PSIADM_KillProc(PStask_ID_t tid, int sig);

/**
 * @brief Resolve nodes.
 *
 * Resolve the nodes marked within the nodelist @a nl. For each node a
 * line giving the node ID and the hostname is printed.
 *
 * @param nl The nodelist describing the node on which the action
 * should take effect.
 *
 * @return No return value.
 */
void PSIADM_Resolve(char *nl);

/**
 * @brief Handle plugins.
 *
 * Handle the plugin @a name on the nodes selected within the nodelist
 * @a nl. Depending on the @a action, the plugin is loaded
 * (PSP_PLUGIN_LOAD) or unloaded (PSP_PLUGIN_REMOVE).
 *
 * @param nl The nodelist describing the node on which the action
 * should take effect.
 *
 * @param name Name of the plugin to handle
 *
 * @param action The action to apply on the requested plugin
 *
 * @return No return value.
 */
void PSIADM_Plugin(char *nl, char *name, PSP_Plugin_t action);

/**
 * @brief Get screen width.
 *
 * Get the screen width of the terminal stdout is connected to.
 *
 * If the TIOCGWINSZ @ref ioctl() is available, it is used to
 * determine the width. Otherwise the COLUMNS environment variable is
 * used to identify the size.
 *
 * If the determined width is smaller than 60, it is set to this
 * minimum value.
 *
 * If both methods cited above failed, the width is set to the default
 * size of 80.
 *
 *
 * @return On success, the actual screen size is returned. If the
 * determination of the current screen size failed, the default width
 * 80 is passed to the calling function. If the determined width is
 * too small, the minimal width 60 is returned.
 *
 * @see ioctl()
 */
int getWidth(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __COMMANDS_H */
