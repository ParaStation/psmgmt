/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PELOGUE_FORWARDER
#define __PELOGUE_FORWARDER

#include "pluginforwarder.h"

/**
 * @brief Execute pelogue script
 *
 * Execute a pelogue script under the control of a
 * pluginforwarder. Information on the governing forwarder is
 * contained in @a fwData. Its userData attribute is expected to hold
 * information on the actual pelogue to execute. @a rerun will provide
 * the reiteration number of the specific function call. This allows
 * to run multiple pelogue (e.g. root and user) under the control of a
 * single forwarder one after the other.
 *
 * While setting up the environment of the actual pelogue to be
 * executed the hook PSIDHOOK_PELOGUE_PREPARE is called allowing for a
 * modification of the environment and the argument vector of the
 * pelogue.
 *
 * This function is intended to act as a pluginforwarder's childFunc.
 *
 * @param fwData Description of the forwarder calling this
 * function. Its userData attribute is expected to hold a pointer to
 * the pelogue description.
 *
 * @param rerun Reiteration number of calling this function
 *
 * @return No return value
 */
void execPElogueScript(Forwarder_Data_t *fwData, int rerun);

#endif  /* __PELOGUE_FORWARDER */
