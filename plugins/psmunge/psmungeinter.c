/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdlib.h>
#include <munge.h>
#include <sys/types.h>

#include "pluginhelper.h"
#include "psmungelog.h"

#include "psmungeinter.h"

/** Default encoding context */
static munge_ctx_t defEncCtx = NULL;

/** Default decoding context */
static munge_ctx_t defDecCtx = NULL;

static int mungeEncCtx(char **cred, munge_ctx_t ctx, const void *buf, int len)
{
    munge_err_t err = munge_encode(cred, ctx, buf, len);

    if (err != EMUNGE_SUCCESS) {
	mlog("%s: encode failed: %s\n", __func__, munge_strerror(err));
	return 0;
    }

    return 1;
}

int psMungeEncode(char **cred)
{
    return mungeEncCtx(cred, defEncCtx, NULL, 0);
}

int psMungeEncodeBuf(char **cred, const void *buf, int len)
{
    return mungeEncCtx(cred, defEncCtx, buf, len);
}

static void mungeLogCredTime(munge_ctx_t ctx)
{
    munge_err_t err;
    time_t dTime, eTime;

    err = munge_ctx_get(ctx, MUNGE_OPT_ENCODE_TIME, &eTime);
    if (err != EMUNGE_SUCCESS) {
	mlog("%s: getting encode time failed: %s\n", __func__,
	     munge_strerror(err));
    } else {
	mlog("%s: encode time '%s'\n", __func__, printTime(eTime));
    }

    err = munge_ctx_get(ctx, MUNGE_OPT_DECODE_TIME, &dTime);
    if (err != EMUNGE_SUCCESS) {
	mlog("%s: getting decode time failed: %s\n", __func__,
	     munge_strerror(err));
    } else {
	mlog("%s: decode time '%s'\n", __func__, printTime(dTime));
    }
}

static int mungeDecCtx(const char *cred, munge_ctx_t ctx, void **buf, int *len,
		       uid_t *uid, gid_t *gid)
{
    munge_err_t err = munge_decode(cred, ctx, buf, len, uid, gid);

    if (err != EMUNGE_SUCCESS) {
	mlog("%s: decode failed: %s\n", __func__, munge_strerror(err));
	if (err == EMUNGE_CRED_EXPIRED) mungeLogCredTime(ctx);
	return 0;
    }

    return 1;
}

int psMungeDecode(const char *cred, uid_t *uid, gid_t *gid)
{
    return mungeDecCtx(cred, defDecCtx, NULL, 0, uid, gid);
}

int psMungeDecodeBuf(const char *cred, void **buf, int *len,
		   uid_t *uid, gid_t *gid)
{
    return mungeDecCtx(cred, defDecCtx, buf, len, uid, gid);
}

static int initDefaultContext(void)
{
    defEncCtx = munge_ctx_create();
    if (!defEncCtx) {
	mlog("%s: creating encoding context failed\n", __func__);
	return 0;
    }

    defDecCtx = munge_ctx_create();
    if (!defDecCtx) {
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

    if (munge_ctx_set(defEncCtx, MUNGE_OPT_ZIP_TYPE, MUNGE_ZIP_BZLIB)
	!= EMUNGE_SUCCESS) {
	mlog("%s: setting munge zip failed\n", __func__);
	return 0;
    }

    if (munge_ctx_set(defEncCtx, MUNGE_OPT_CIPHER_TYPE, MUNGE_CIPHER_NONE)
	!= EMUNGE_SUCCESS) {
	mlog("%s: setting munge zip failed\n", __func__);
	return 0;
    }
    */

    return 1;
}

bool initMunge(void)
{
    char *cred = NULL;
    uid_t uid;
    gid_t gid;

    if (!initDefaultContext()) return false;

    /* test munge functionality */
    if (!psMungeEncode(&cred)) goto INIT_ERROR;
    if (!psMungeDecode(cred, &uid, &gid)) goto INIT_ERROR;
    free(cred);

    return true;

INIT_ERROR:
    if (defEncCtx) munge_ctx_destroy(defEncCtx);
    if (defDecCtx) munge_ctx_destroy(defDecCtx);
    if (cred) free(cred);

    return false;
}

void finalizeMunge(void)
{
    /* free all allocated memory */
    if (defEncCtx) munge_ctx_destroy(defEncCtx);
    if (defDecCtx) munge_ctx_destroy(defDecCtx);
}
