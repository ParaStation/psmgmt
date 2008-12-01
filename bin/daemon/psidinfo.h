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
 * Handle info requests to the ParaStation daemon.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDINFO_H
#define __PSIDINFO_H

#include "psprotocol.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Initialize info stuff
 *
 * Initialize the info request framework. This registers the necessary
 * message handlers.
 *
 * @return No return value.
 */
void initInfo(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDINFO_H */
