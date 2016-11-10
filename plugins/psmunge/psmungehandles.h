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

#ifndef __PSMUNGE__HANDLES
#define __PSMUNGE__HANDLES

int (*psMungeEncode)(char **);
int (*psMungeDecode)(const char *, uid_t *, gid_t *);
int (*psMungeDecodeBuf)(const char *, void **, int *, uid_t *, gid_t *);

#endif
