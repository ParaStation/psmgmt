/*
 * ParaStation
 *
 * Copyright (C) 2014 - 2015 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_PAM__HANDLES
#define __PS_PAM__HANDLES

#include "pspamdef.h"

void (*psPamAddUser)(char *, char *, int);
void (*psPamDeleteUser)(char *, char *);
void (*psPamSetState)(char *, char *, int);

#endif
