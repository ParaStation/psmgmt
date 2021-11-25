/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PLUGIN_LIB_PTY
#define __PLUGIN_LIB_PTY

#include <stdbool.h>
#include <sys/types.h>

/**
 * @brief Set pty's ownership
 *
 * Make the user with user ID @a uid the owner of the tty named by @a
 * tty and set its permissions accordingly. If the group "tty" exists
 * on the system, @a tty's group ownership is set to "tty". Otherwise
 * it is set to the value provided in @a gid.
 *
 * Depending on the existance of the group "tty" the permissions of
 * the tty are set to rw--w---- ("tty" exists) or rw--w--w-.
 *
 * @param uid The user ID to owership is given to
 *
 * @param gid The group ID the ownership is given to if the group
 * "tty" does not exist
 *
 * @param tty Name of the tty to handle
 *
 * @return Returns true on success or false otherwise
 */
bool pty_setowner(uid_t uid, gid_t gid, const char *tty);

/**
 * @brief Make pty the controlling terminal
 *
 * Makes the slave pty the controlling terminal. @a *ttyfd contains
 * the descriptor for the slave side of a pseudo terminal. Upon
 * success the descriptor of the resulting controlling terminal will
 * be stored in @a *ttyfd. @a tty is the device name of the slave
 * side of the pseudo terminal.
 *
 * @param ttyfd Slave pty to become the controlling terminal
 *
 * @param tty Device name of the slave pty
 *
 * @return Returns true on success or false otherwise
 */
bool pty_make_controlling_tty(int *ttyfd, const char *tty);

#endif  /* __PLUGIN_LIB_PTY */
