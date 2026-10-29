/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PLUGIN_LIB_PTY
#define __PLUGIN_LIB_PTY

#include <stdbool.h>

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
