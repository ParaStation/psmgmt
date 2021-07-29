/*
 * ParaStation
 *
 * Copyright (C) 2006-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
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

#include <stdbool.h>
#include <stdint.h>

#include "pscpu.h"
#include "psprotocol.h"

/**
 * @brief Initialize hardware stuff
 *
 * Initialize the hardware framework. This registers the necessary
 * message handlers and determines various hardware parameters of the
 * local node and pushes them into the PSIDnodes facility. The
 * parameters contain NUMA domains, distribution of hardware threads
 * over NUMA domains, and distances between NUMA domains. Furthermore
 * number and their distribution over NUMA domains of PCIe devices
 * like GPUs and high performance NICs like HCAs or HFIs are
 * determined.
 *
 * @return No return value.
 */
void PSIDhw_init(void);

/**
 * @brief Re-initialize hardware stuff
 *
 * Re-Initialize core hardware detection. This triggers again the
 * determination of various hardware parameters of the local node and
 * pushes them into the PSIDnodes facility. The parameters contain
 * NUMA domains, distribution of hardware threads over NUMA domains,
 * and distances between NUMA domains.
 *
 * This might be used after the HWLOC_XMLFILE environment variable was
 * tweaked in order to mimik a different hardware platform.
 *
 * Keep in mind that identification of PCIe devices will be
 * invalidated by tweaking the HWLOC_XMLFILE environment variable,
 * too. Thus, the corresponding information will be removed from the
 * PSIDnodes facility.
 *
 * @return No return value.
 */
void PSIDhw_reInit(void);

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
 * @brief Send hardware counters
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
 * @return No return value
 */
void PSID_sendCounter(DDTypedBufferMsg_t *inmsg);

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
int PSIDhw_getHWthreads(void);

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
int PSIDhw_getCores(void);

/**
 * Type used to identify PCI devices in checkPCIDev() and callers,
 * i.e. @ref PSID_getNumPCIDevs() and @ref PSID_getPCISets()
 */
typedef struct {
    uint16_t vendor_id;       /**< PCIe vendor ID */
    uint16_t device_id;       /**< PCIe device ID */
    uint16_t subvendor_id;    /**< PCIe subsystem vendor ID */
    uint16_t subdevice_id;    /**< PCIe subsystem device ID */
} PCI_ID_t;

/**
 * @brief Get number of PCI devices of a specific kind
 *
 * Determine the number of PCI devices conforming the definition of @a
 * ID_list. Each PCI devices as reported by hwloc is passed to @ref
 * checkPCIDev() with @a ID_list as the second argument and will be
 * counted if this function returns true.
 *
 * hwloc is initialized implicitly if this has not happened before.
 *
 * If for some reason the hwloc framework cannot be initialized,
 * exit() is called.
 *
 * @param ID_list Zero-terminated array of PCI vendor, device,
 * subvendor and subdevice IDs identifying the PCI devices to handle
 *
 * @return Number of PCI devices of the selected kind
 */
uint16_t PSIDhw_getNumPCIDevs(PCI_ID_t ID_list[]);

/**
 * @brief Get the PCI device sets for all NUMA nodes
 *
 * Determine the PCI device sets for devices conforming to @a ID_list
 * for each NUMA domain and return them as an array. This utilizes the
 * hwloc framework.
 *
 * PCI devices are identified by utilizing @a ID_list. Depending on
 * the flag @a PCIorder devices are either numbered in PCI device
 * order or in hwloc order. If @a PCIorder is true, PCI devices order
 * is used utilizing the map created by @ref getPCIorderMap().
 *
 * By using @ref getNUMADoms() and @ref getNumPCIDevs() this
 * implicitly initializes hwloc if this has not happened before and
 * could result in an exit().
 *
 * The array returned is indexed by NUMA domain numbers. It is
 * allocated via malloc() and has to be free()ed by the caller once it
 * is no longer needed. Thus, it is well suited to be registered to
 * the PSIDnodes facility via PSIDnodes_setGPUSets() or
 * PSIDnodes_setNICSets().
 *
 * @param PCIorder Flag to trigger PCI device order for numbering the
 * PCI devices to handle
 *
 * @param ID_list Zero-terminated array of PCI vendor, device,
 * subvendor and subdevice IDs identifying the PCI devices to handle
 *
 * @return On success, the array of CPU set is returned; on error, NULL
 * might be returned
 */
PSCPU_set_t * PSIDhw_getPCISets(bool PCIorder, PCI_ID_t ID_list[]);

#endif /* __PSIDHW_H */
