/*
 * ParaStation
 *
 * Copyright (C) 2006-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Functions handling the communication hardware
 */
#ifndef __PSIDHW_H
#define __PSIDHW_H

#include "pscpu.h"
#include "psprotocol.h"

/**
 * @brief Initialize hardware stuff
 *
 * Initialize the communication hardware framework. This registers the
 * necessary message handlers and determines various hardware
 * parameters of the local node and pushes them into the PSIDnodes
 * facility. The parameters contain NUMA domains, distribution of
 * hardware threads over NUMA domains, number and distribution over
 * NUMA domains of GPUs and high performance NICs like HCAs or HFIs.
 *
 * @return No return value.
 */
void initHW(void);

/**
 * @brief Init all communication hardware.
 *
 * Initialize all the configured communication hardware. Various
 * parameters have to be set before. This is usually done by reading
 * and parsing the configuration file within @ref
 * PSID_readConfigFile(). For further details take a look on the
 * source code.
 *
 * The actual initialization of the various hardware types defined is
 * done via calls to the internal switchHW() function.
 *
 * @return No return value.
 *
 * @see PSID_readConfigFile()
 */
void PSID_startAllHW(void);

/**
 * @brief Stop all communication hardware.
 *
 * Stop and bring down all the configured and initialized
 * communication hardware. Various parameters have to be set
 * before. This is usually done by reading and parsing the
 * configuration file within @ref PSID_readConfigFile(). For further
 * details take a look on the source code.
 *
 * The actual stopping of the various hardware types defined is done
 * via calls to the internal switchHW() function.
 *
 * @return No return value.
 *
 * @see PSID_readConfigFile()
 */
void PSID_stopAllHW(void);

/**
 * @brief Get hardware counters.
 *
 * Read out the hardware counters of the hardware corresponding to the
 * information in the requesting message @a inmsg. The value of the
 * counter is determined via calling the script registered to this
 * hardware. The answering message is created within the script's
 * callback-function and sent back to the requester.
 *
 * Depending on the type-value of @a insmsg, either a header line
 * describing the different values of the counter line is created (@a
 * type = PSP_INFO_COUNTHEADER) or the actual counter line is
 * generated.
 *
 * @param insmsg The requesting message containing the hardware, the
 * actual type of information, the requester, etc.
 *
 * @return No return value.
 */
void PSID_getCounter(DDTypedBufferMsg_t *inmsg);

/**
 * @brief Get number of hardware threads
 *
 * Determine the number of hardware threads. This utilizes the hwloc
 * framework and returns the number of PUs detected there.
 *
 * hwloc is initialized implicitly if this has not happened before.
 *
 * If for some reason the hwloc framework cannot be initialized,
 * exit() is called.
 *
 * @return On success, the number of virtual processors is returned
 */
int PSID_getHWthreads(void);

/**
 * @brief Get number of physical cores
 *
 * Determine the number of physical cores. This utilizes the hwloc
 * framework and returns the number of cores detected there.
 *
 * The number of physical cores might differ from the number of
 * hardware threads on any platform supporting SMT, like e.g. Intel
 * CPUs supporting Hyper-Threading Technology, AMD CPUs starting with
 * the Zen generation or modern Power or ARM CPUs.
 *
 * hwloc is initialized implicitly if this has not happened before.
 *
 * If for some reason the hwloc framework cannot be initialized,
 * exit() is called.
 *
 * @return On success, the number of physical cores is returned
 */
int PSID_getPhysCores(void);

#endif /* __PSIDHW_H */
