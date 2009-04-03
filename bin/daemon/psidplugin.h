/*
 *               ParaStation
 *
 * Copyright (C) 2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * Helper functions for plugin handling.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDPLUGIN_H
#define __PSIDPLUGIN_H

#include "pstask.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Initialize partition stuff
 *
 * Initialize the partition handling framework. This also registers
 * the necessary message handlers.
 *
 * @return No return value.
 */
void initPlugins(void);

/**
 * @brief Send list of requests.
 *
 * Send a list of information on the plugins currently loaded in the
 * local daemon. All information about a single plugin (i.e. name,
 * version, and triggering plugins) is given back in a character
 * string.
 *
 * @param dest Task ID of process waiting for answer.
 *
 * @return No return value.
 */
void PSID_sendPluginLists(PStask_ID_t dest);


#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDPLUGIN_H */
