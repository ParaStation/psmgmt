/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * Spawning of client processes and forwarding for the ParaStation daemon
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDSPAWN_H
#define __PSIDSPAWN_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Initialize spawning stuff
 *
 * Initialize the spawning and forwarding framework. This registers
 * the necessary message handlers.
 *
 * @return No return value.
 */
void initSpawn(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDSPAWN_H */
