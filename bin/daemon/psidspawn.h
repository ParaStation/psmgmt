/*
 *               ParaStation
 * psidspawn.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidspawn.h,v 1.3 2003/10/08 14:51:21 eicker Exp $
 *
 */
/**
 * \file
 * Spawning of client processes and forwarding for the ParaStation daemon
 *
 * $Id: psidspawn.h,v 1.3 2003/10/08 14:51:21 eicker Exp $
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

/** @todo Documentation */

/**
 * @brief Frontend to execv(3).
 *
 * Frontend to execv(3). Retry exec on failure after a short delay.
 *
 *
 * @param path
 *
 * @param argv
 *
 *
 * @return Like the execv(3).
 *
 * @see execv(3)
 */
int PSID_execv( const char *path, char *const argv[]);

/*----------------------------------------------------------------------*/
/*
 * PSID_stat
 *
 *  frontend to syscall stat. Retry stat(2) on failure after a short delay
 *  (workaround for automounter problems)
 *  RETURN: like the syscall stat(2)
 */
int PSID_stat(char *file_name, struct stat *buf);

/*----------------------------------------------------------------------*/
/*
 * PStask_spawn
 *
 *  executes the argv[0] with parameters argv[1]..argv[argc-1]
 *  in working directory workingdir with userid uid
 *  RETURN: 0 on success with childpid set to the pid of the new process
 *          errno  when an error occurs
 */
int PSID_spawnTask(PStask_t *forwarder, PStask_t *client);
/* spawns a process with the definitions in task on the local node */

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDSPAWN_H */
