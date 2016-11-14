/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PSMUNGE__MAIN
#define __PSMUNGE__MAIN

/**
 * @brief Constructor for psmunge library.
 *
 * @return No return value.
 */
void __attribute__ ((constructor)) startPsmunge();

/**
 * @brief Destructor for psmunge library.
 *
 * @return No return value.
 */
void __attribute__ ((destructor)) stopPsmunge();

/**
 * @brief Initialize the psmunge plugin.
 *
 * @return Returns 1 on error and 0 on success.
 */
int initialize(void);

/**
 * @brief Free left memory, final cleanup.
 *
 * After this function we will be unloaded.
 *
 * @return No return value.
 */
void cleanup(void);

int mungeEncode(char **cred);

int mungeEncodeBuf(char **cred, const void *buf, int len);

int mungeEncodeCtx(char **cred, munge_ctx_t ctx, const void *buf, int len);

int mungeDecode(const char *cred, uid_t *uid, gid_t *gid);

int mungeDecodeBuf(const char *cred, void **buf, int *len,
		    uid_t *uid, gid_t *gid);

int mungeDecodeCtx(const char *cred, munge_ctx_t ctx, void **buf,
		    int *len, uid_t *uid, gid_t *gid);
#endif
