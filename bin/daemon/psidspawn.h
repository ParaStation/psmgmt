/*
 *               ParaStation
 * psidspawn.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidspawn.h,v 1.4 2003/10/30 16:30:45 eicker Exp $
 *
 */
/**
 * \file
 * Spawning of client processes and forwarding for the ParaStation daemon
 *
 * $Id: psidspawn.h,v 1.4 2003/10/30 16:30:45 eicker Exp $
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
 * @brief Frontend to execv(3).
 *
 * Frontend to execv(3). Retry execv() on failure after a delay of
 * 400ms. With 5 tries at all this results in a total trial time of
 * about 2sec.
 *
 * @param path The pathname of the file to be executed.
 *
 * @param argv Array of pointers to null-terminated strings that
 * represent the argument list available to the new program. The first
 * argument, by convention, should point to the file name associated
 * with the file being executed. The array of pointers must be
 * terminated by a NULL pointer.
 *
 *
 * @return Like the execv(3) return value.
 *
 * @see execv(3)
 */
int PSID_execv( const char *path, char *const argv[]);

/**
 * @brief Frontend to stat(2).
 *
 * Frontend to stat(2). Retry stat() on failure after a delay of
 * 400ms. With 5 tries at all this results in a total trial time of
 * about 2sec.
 *
 * @param file_name The name of the file to stat. This might be a
 * absolute or relative path to the file.
 *
 * @param buf Buffer to hold the returned stat information of the file.
 *
 * @return Like the stat(2) return value.
 *
 * @see stat(2)
 */
int PSID_stat(char *file_name, struct stat *buf);

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
