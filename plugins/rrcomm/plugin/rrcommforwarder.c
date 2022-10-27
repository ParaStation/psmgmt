/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file RRComm functionality running either in the psidforwarder
 * process itself or being executed while setting it up
 */
#include "rrcommforwarder.h"

#include "psidhook.h"

#include "rrcommlog.h"

static int hookExecForwarder(void *data)
{
    return 0;
}

static int hookExecClient(void *data)
{
    return 0;
}

static int hookFrwrdInit(void *data)
{
    return 0;
}

static int hookFrwrdExit(void *data)
{
    return 0;
}

bool attachRRCommForwarderHooks(void)
{
    if (!PSIDhook_add(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder)) {
	flog("attaching 'PSIDHOOK_EXEC_FORWARDER' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_EXEC_CLIENT, hookExecClient)) {
	flog("attaching 'PSIDHOOK_EXEC_CLIENT' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_FRWRD_INIT, hookFrwrdInit)) {
	flog("attaching 'PSIDHOOK_FRWRD_INIT' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_FRWRD_EXIT, hookFrwrdExit)) {
	flog("attaching 'PSIDHOOK_FRWRD_EXIT' failed\n");
	return false;
    }

    return true;
}

void detachRRCommForwarderHooks(bool verbose)
{
    if (!PSIDhook_del(PSIDHOOK_FRWRD_EXIT, hookFrwrdExit)) {
	if (verbose) flog("unregister 'PSIDHOOK_FRWRD_EXIT' failed\n");
    }
    if (!PSIDhook_del(PSIDHOOK_FRWRD_INIT, hookFrwrdInit)) {
	if (verbose) flog("unregister 'PSIDHOOK_FRWRD_INIT' failed\n");
    }
    if (!PSIDhook_del(PSIDHOOK_EXEC_CLIENT, hookExecClient)) {
	if (verbose) flog("unregister 'PSIDHOOK_EXEC_CLIENT' failed\n");
    }
    if (!PSIDhook_del(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder)) {
	if (verbose) flog("unregister 'PSIDHOOK_EXEC_FORWARDER' failed\n");
    }
}
