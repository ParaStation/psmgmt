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

#include "psprotocol.h"
#include "pscpu.h"

#define PSNUMANODE_MAX 16
#define PSGPU_MAX 32

/**
 * @brief Initialize hardware stuff
 *
 * Initialize the communication hardware framework. This registers the
 * necessary message handlers.
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
 * @param insmsg The requesting message containig the hardwre, the
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

/**
 * @brief Get number of NUMA nodes
 *
 * Determine the number of NUMA nodes. This utilizes the hwloc
 * framework and returns the number of NUMA nodes detected there
 * or 1 if no NUMA nodes are detected, which is the normal case
 * for UMA systems.
 *
 * This relies on the PCI device node of the GPU card in the hwloc topology
 * to have PCI Class ID 0x0302 (3D).
 *
 * hwloc is initialized implicitly if this has not happened before.
 *
 * If for some reason the hwloc framework cannot be initialized,
 * exit() is called.
 *
 * @return On success, the number of NUMA nodes is returned
 */
int PSID_getNUMAnodes(void);

/**
 * @brief Get the CPU masks for all NUMA nodes
 *
 * Determine the CPU mask for each NUMA node and returns them as array.
 * This utilizes the hwloc framework.
 *
 * By using @a PSID_getNUMAnodes() this implicitly initializes hwloc
 * if this has not happened before and could result in exit().
 *
 * The array returned is indexed by the NUMA node numbers. It is owned by
 * the psidhw framework and should not be changed or freed elsewhere.
 *
 * @param psorder  Flag to return the masks in ParaStation CPU order
 *
 *                 (thus mapped using cpu map)
 *
 * @return On success, the array of CPU masks is returned
 */
PSCPU_set_t* PSID_getCPUmaskOfNUMAnodes(bool psorder);

/**
 * @brief Get number of GPUs
 *
 * Determine the number of graphics processing units. This utilizes the hwloc
 * framework and returns the number of gpus detected there.
 *
 * This relies on the PCI device node of the GPU card in the hwloc topology
 * to have PCI Class ID 0x0302 (3D).
 *
 * hwloc is initialized implicitly if this has not happened before.
 *
 * If for some reason the hwloc framework cannot be initialized,
 * exit() is called.
 *
 * @return On success, the number of GPUs is returned
 */
int PSID_getGPUs(void);

/**
 * @brief Translate the gpu id in hwloc order to PCI address order
 *
 * hwloc is initialized implicitly if this has not happened before.
 *
 * If for some reason the hwloc framework cannot be initialized,
 * exit() is called.
 *
 * @return On success, the id in pci address order is returned
 */
uint16_t PSID_getGPUinPCIorder(uint16_t gpu);

/**
 * @brief Get the GPU masks for all NUMA nodes
 *
 * Determine the GPU mask for each NUMA node and returns them as array.
 * This utilizes the hwloc framework.
 *
 * By using @a PSID_getNUMAnodes() this implicitly initializes hwloc
 * if this has not happened before and could result in exit().
 *
 * The array returned is indexed by the NUMA node numbers. It is owned by
 * the psidhw framework and should not be changed or freed elsewhere.
 *
 * This abuses the PSCPU framework to store the GPU masks. Just use it as
 * the GPUs where single core CPUs.
 *
 * @return On success, the array of GPU masks is returned
 */
PSCPU_set_t* PSID_getGPUmaskOfNUMAnodes(void);

#endif /* __PSIDHW_H */
