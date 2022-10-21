/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "rrcomm.h"

#include <errno.h>

int RRC_init(void)
{
    errno = ENOSYS;
    return -1;
}

/** @doctodo */
static bool instantError = false;

bool RRC_instantError(bool flag)
{
    bool ret = instantError;
    instantError = flag;

    return ret;
}

ssize_t RRC_send(int rank, char *buf, size_t bufSize)
{
    errno = ENOSYS;
    return -1;
}

ssize_t RRC_recv(int *rank, char *buf, size_t bufSize)
{
    errno = ENOSYS;
    return -1;
}

void RRC_finalize(void)
{}
