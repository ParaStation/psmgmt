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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <munge.h>
#include <string.h>

#include "plugin.h"

#include "psmungelog.h"

#include "psmunge.h"

munge_ctx_t defEncCtx;
munge_ctx_t defDecCtx;

static int isInit = 0;

/** psid plugin requirements */
char name[] = "psmunge";
int version = 2;
int requiredAPI = 109;
plugin_dep_t dependencies[1];

void startPsmunge()
{
    /* we have no dependencies */
    dependencies[0].name = NULL;
    dependencies[0].version = 0;
}

void stopPsmunge()
{
    /* release the logger */
    logger_finalize(psmungelogger);
}

int mungeEncode(char **cred)
{
    return mungeEncodeCtx(cred, defEncCtx, NULL, 0);
}

int mungeEncodeBuf(char **cred, const void *buf, int len)
{
    return mungeEncodeCtx(cred, defEncCtx, buf, len);
}

int mungeEncodeCtx(char **cred, munge_ctx_t ctx, const void *buf, int len)
{
    munge_err_t err;

    if ((err = munge_encode(cred, ctx, buf, len)) != EMUNGE_SUCCESS) {
	mlog("%s: encode failed: %s\n", __func__, munge_strerror(err));
	return 0;
    }

    return 1;
}

int mungeDecode(const char *cred, uid_t *uid, gid_t *gid)
{
    return mungeDecodeCtx(cred, defDecCtx, NULL, 0, uid, gid);
}

int mungeDecodeBuf(const char *cred, void **buf, int *len,
		    uid_t *uid, gid_t *gid)
{
    return mungeDecodeCtx(cred, defDecCtx, buf, len, uid, gid);
}

int mungeDecodeCtx(const char *cred, munge_ctx_t ctx, void **buf,
		    int *len, uid_t *uid, gid_t *gid)
{
    munge_err_t err;

    if ((err = munge_decode(cred, ctx, buf, len, uid, gid)) != EMUNGE_SUCCESS) {
	mlog("%s: encode failed: %s\n", __func__, munge_strerror(err));
	return 0;
    }

    return 1;
}

static int initDefaultContext()
{
    if (!(defEncCtx = munge_ctx_create())) {
	mlog("%s: creating encoding context failed\n", __func__);
	return 0;
    }

    if (!(defDecCtx = munge_ctx_create())) {
	munge_ctx_destroy(defEncCtx);
	mlog("%s: creating decoding context failed\n", __func__);
	return 0;
    }

    /*
     * TODO: read and set options from psmunge config file
    char *socket = NULL;

     * do we need to set the socket ourself???
     *
    if (munge_ctx_set(defEncCtx, MUNGE_OPT_SOCKET, socket) != EMUNGE_SUCCESS) {
	mlog("%s: setting munge socket '%s' failed\n", __func__, socket);
	return 0;
    }

    if (munge_ctx_set(defEncCtx, MUNGE_OPT_ZIP_TYPE, MUNGE_ZIP_BZLIB) != EMUNGE_SUCCESS) {
	mlog("%s: setting munge zip failed\n", __func__);
	return 0;
    }

    if (munge_ctx_set(defEncCtx, MUNGE_OPT_CIPHER_TYPE, MUNGE_CIPHER_NONE) != EMUNGE_SUCCESS) {
	mlog("%s: setting munge zip failed\n", __func__);
	return 0;
    }
    */

    return 1;
}

int initialize(void)
{
    char *cred;
    uid_t uid;
    gid_t gid;

    /* init the logger (log to syslog) */
    initLogger("psmunge", NULL);

    /* we need to have root privileges */
    if (getuid() != 0) {
	fprintf(stderr, "%s: psmunge must have root privileges\n", __func__);
	return 1;
    }

    if (!(initDefaultContext())) return 1;

    /* test munge functionality */
    if (!(mungeEncode(&cred))) goto INIT_ERROR;
    if (!(mungeDecode(cred, &uid, &gid))) goto INIT_ERROR;
    free(cred);

    isInit = 1;
    mlog("(%i) successfully started\n", version);
    return 0;

INIT_ERROR:
    munge_ctx_destroy(defEncCtx);
    munge_ctx_destroy(defDecCtx);
    return 1;
}

void finalize(void)
{

}

void cleanup(void)
{
    if (!isInit) return;

    /* free all malloced memory */
    munge_ctx_destroy(defEncCtx);
    munge_ctx_destroy(defDecCtx);

    mlog("...Bye.\n");
}
