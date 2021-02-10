/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Basic communication operations for user-programs, daemon,
 * plugins, and forwarders
 */
#ifndef __PSCIO_H
#define __PSCIO_H

#include <stdbool.h>

/**
 * @brief Switch file-descriptor's blocking mode
 *
 * If the flag @a block is true, the file-descriptor @a fd is brought
 * into blocking mode, i.e. the @ref O_NONBLOCK flag is removed from
 * the file-descriptor. If block is false, the flag is set.
 *
 * @param fd File descriptor to manipulate
 *
 * @param block Flag the blocking mode
 *
 * @return No return value
 */
void PSCio_setFDblock(int fd, bool block);


#endif  /* __PSCIO_H */
