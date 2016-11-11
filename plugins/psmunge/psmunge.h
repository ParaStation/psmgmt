/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PSMUNGE__MAIN
#define __PSMUNGE__MAIN

int mungeEncode(char **cred);

int mungeEncodeBuf(char **cred, const void *buf, int len);

int mungeDecode(const char *cred, uid_t *uid, gid_t *gid);

int mungeDecodeBuf(const char *cred, void **buf, int *len,
		    uid_t *uid, gid_t *gid);

#endif  /* __PSMUNGE__MAIN */
