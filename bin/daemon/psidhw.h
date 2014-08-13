/*
 * ParaStation
 *
 * Copyright (C) 2006-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * \file
 * Functions handling the communication hardware
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDHW_H
#define __PSIDHW_H

#include "psprotocol.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

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
 * @brief Set hardware parameter.
 *
 * This function is actually doing nothing.
 *
 * @warning Deprecated. Was used for Myrinet-support within
 * ParaStation 3. Don't use this.
 *
 * @deprecated Was used for Myrinet-support within ParaStation 3.
 *
 * @return No return value.
 */
void PSID_setParam(int hw, PSP_Option_t option, PSP_Optval_t value)
    __attribute__((deprecated));

/**
 * @brief Get hardware parameter.
 *
 * @warning Deprecated. Was used for Myrinet-support within
 * ParaStation 3. Don't use this.
 *
 * @deprecated Was used for Myrinet-support within ParaStation 3.
 *
 * @return Will always return -1.
 */
PSP_Optval_t PSID_getParam(int hw, PSP_Option_t option)
    __attribute__((deprecated));

/**
 * @brief Get number of virtual CPUs.
 *
 * Determine the number of virtual CPUs. This is done via a call to
 * sysconfig(_SC_NPROCESSORS_CONF).
 *
 * If for some reason the number of virtual CPUs cannot be determined,
 * i.e. the number reported is 0, after some seconds of sleep() the
 * determination is repeated. If this fails finally, exit() is called.
 *
 * @return On success, the number of virtual processors is
 * returned.
 */
long PSID_getVirtCPUs(void);

/**
 * @brief Get number of physical CPUs.
 *
 * Determine the number of physical CPUs. The number of physical CPUs
 * might differ from the number of virtual CPUs e.g. on newer Pentium
 * platforms which support the Hyper-Threading Technology.
 *
 * If for some reason the number of physical CPUs cannot be
 * determined, i.e. the number reported is 0, after some seconds of
 * sleep() the determination is repeated. If this fails finally,
 * exit() is called.
 *
 * @return On success, the number of physical CPUs is returned.
 */
long PSID_getPhysCPUs(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDHW_H */
