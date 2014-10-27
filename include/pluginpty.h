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

#ifndef __PLUGIN_LIB_PTY
#define __PLUGIN_LIB_PTY

void pty_setowner(uid_t uid, gid_t gid, const char *tty);
void pty_make_controlling_tty(int *ttyfd, const char *tty);

#endif
