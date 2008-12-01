/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * Handle option requests to the ParaStation daemon.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDOPTIONS_H
#define __PSIDOPTIONS_H

#include "psprotocol.h"
#include "psnodes.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Initialize option stuff
 *
 * Initialize the options framework. This registers the necessary
 * message handlers.
 *
 * @return No return value.
 */
void initOptions(void);

/**
 * @brief Send some options.
 *
 * Send some options upon startup of a daemon-daemon connection to @a
 * destnode's daemon.
 *
 * @param destnode The node the options should be send to.
 *
 * @return No return value.
 */
void send_OPTIONS(PSnodes_ID_t destnode);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDOPTIONS_H */
