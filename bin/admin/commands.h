/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
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
 * concerning the left out processes is displayed.
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

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __COMMANDS_H */
