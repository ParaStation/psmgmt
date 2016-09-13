/*
 * ParaStation
 *
 * Copyright (C) 2012-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_ACCOUNT_KVS
#define __PS_ACCOUNT_KVS

/* file handle for memory debug output */
extern FILE *memoryDebug;

char *set(char *key, char *value);

char *unset(char *key);

char *help(void);

char *show(char *key);

#endif
