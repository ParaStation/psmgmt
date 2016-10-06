/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
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
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

#include "pmilog.h"
#include "pmiprovider.h"

/* socket used by the kvsprovider */
static int providerSocket = -1;

/* socket used by the psidforwarder */
static int kvsForwarderSocket = -1;

void closeKVSForwarderSock(void)
{
    close(kvsForwarderSocket);
}

void setupKVSProviderComm(void)
{
    int socketfds[2];
    char env[50];

    /* setup communication between psidforwarder and KVS provider */
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, socketfds)<0) {
	mwarn(errno, "%s: socketpair()", __func__);
	return;
    }

    kvsForwarderSocket = socketfds[0];
    providerSocket = socketfds[1];

    snprintf(env, sizeof(env), "%d", providerSocket);
    setenv("__PMI_PROVIDER_FD", env, 1);
}

void handleServiceExit(PSLog_Msg_t *msg)
{
    if (kvsForwarderSocket == -1) return;

    /* closing of the fd will trigger the kvsprovider to exit */
    close(kvsForwarderSocket);
    kvsForwarderSocket = -1;
}
