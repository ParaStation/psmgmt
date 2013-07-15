/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_MOM_COLLECT
#define __PS_MOM_COLLECT

#include <sys/time.h>
#include <sys/resource.h>

#include "psmomlist.h"

/* save changing job independent information e.g. load */
Data_Entry_t infoData;

/* save static job independent information e.g. number of cpus on node */
Data_Entry_t staticInfoData;

/**
 * @brief Collect various variable informations.
 *
 * Wrapper for all collect functions.
 *
 * @return No return value.
 */
void updateInfoList(int all);

/**
 * @brief Initialize the info list.
 *
 * Initialize the info list and update some static information.
 *
 * @return No return value.
 */
void initInfoList();

/**
 * Set the psmom job state.
 *
 * TODO: check if states are correct
 * (in torque busy is dependent on the load not on the jobs)
 * Valid states are "down", "busy" and "free".
 *
 * @param state The new state to set. If NULL the previous state will be set
 * again.
 *
 * @return No return value.
 */
void setPsmomState(char *state);

#endif
