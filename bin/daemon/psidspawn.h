/*
 *               ParaStation
 * psidspawn.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidspawn.h,v 1.5 2003/12/10 16:44:01 eicker Exp $
 *
 */
/**
 * \file
 * Spawning of client processes and forwarding for the ParaStation daemon
 *
 * $Id: psidspawn.h,v 1.5 2003/12/10 16:44:01 eicker Exp $
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

#include "pstask.h"

/**
 * @brief Spawn a new process.
 *
 * Spawn a new process described by @a client. In order to do this,
 * first of all a forwarder is created that sets up a sendbox for the
 * client process to run in. The the actual client process is started
 * within this sandbox.
 *
 * All necessary information determined during startup of the
 * forwarder and client process is stored within the corresponding
 * task structures. For the forwarder this includes the task ID and
 * the file descriptor connecting the local daemon to the
 * forwarder. For the client only the task ID is stored.
 *
 * @param forwarder Task structure describing the forwarder process to
 * create.
 *
 * @param client Task structure describing the actual client process
 * to create.
 *
 * @return On success, 0 is returned. If something went wrong, a value
 * different from 0 is returned. This value might be interpreted as an
 * errno describing the problem that occurred during the spawn.
 */
int PSID_spawnTask(PStask_t *forwarder, PStask_t *client);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDSPAWN_H */
