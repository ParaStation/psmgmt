/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * This file contains the implementation of the PMIx server interface
 * The pmix server library is initialized and used only in the PMix server.
 */
#define _GNU_SOURCE
#include "pspmixserver.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <pmix_server.h>
#include <pmix_version.h>
#include <pmix.h>

#if PMIX_VERSION_MAJOR >= 4
#include <hwloc.h>
#endif

#include "list.h"
#include "timer.h"
#include "pluginmalloc.h"
#include "pluginstrv.h"
#include "pluginvector.h"
#if PMIX_VERSION_MAJOR >= 4
#include "pluginhelper.h"

#include "psidnodes.h"
#endif

#include "pspmixlog.h"
#include "pspmixservice.h"

/* allow walking throu the environment */
extern char **environ;

/** Initialisation flag */
static bool initialized = false;

/** Generic type for callback data */
typedef struct {
    pmix_status_t status;
    volatile bool filled;
    pmix_info_t *info;
    size_t ninfo;
} mycbdata_t;

/** Generic type for callback function */
typedef struct {
    pmix_op_cbfunc_t cbfunc;
    void *cbdata;
} mycbfunc_t;

/** Setting up data for callback routines */
#define INIT_CBDATA(d, n) do {     \
    memset(&(d), 0, sizeof(d));    \
    (d).ninfo = n;                 \
    if (n) PMIX_INFO_CREATE((d).info, n); \
} while(0)

/** Waiting for data to be filled by callback function */
#define WAIT_FOR_CBDATA(d) while(!(d).filled) usleep(10)

/** Set data to be available (used in callback function) */
#define SET_CBDATA_AVAIL(d) (d)->filled = true

/** Setting up data for callback routines */
#define DESTROY_CBDATA(d) if ((d).ninfo) PMIX_INFO_FREE((d).info, (d).ninfo)

#if 0
/* Create a string representation of a typed pmix value */
static const char * encodeValue(const pmix_value_t *val, pmix_rank_t rank)
{
    char buffer[SBUFSIZE];

    int res = 0;

    switch(val->type) {
	case PMIX_VAL_TYPE_int:
	    res = snprintf(buffer, SBUFSIZE, "%d:int:%d", rank,
		    PMIX_VAL_FIELD_int(val));
	    break;
	case PMIX_VAL_TYPE_uint32_t:
	    res = snprintf(buffer, SBUFSIZE, "%d:uint32_t:%u", rank,
		    PMIX_VAL_FIELD_uint32_t(val));
	    break;
	case PMIX_VAL_TYPE_uint16_t:
	    res = snprintf(buffer, SBUFSIZE, "%d:uint16_t:%hu", rank,
		    PMIX_VAL_FIELD_uint16_t(val));
	    break;
	case PMIX_VAL_TYPE_string:
	    res = snprintf(buffer, SBUFSIZE, "%d:string:%s", rank,
		    PMIX_VAL_FIELD_string(val));
	    break;
	case PMIX_VAL_TYPE_float:
	    res = snprintf(buffer, SBUFSIZE, "%d:float:%f", rank,
		    PMIX_VAL_FIELD_float(val));
	    break;
	case PMIX_VAL_TYPE_byte:
	    res = snprintf(buffer, SBUFSIZE, "%d:byte:%hhu", rank,
		    PMIX_VAL_FIELD_byte(val));
	    break;
	case PMIX_VAL_TYPE_flag:
	    res = snprintf(buffer, SBUFSIZE, "%d:flag:%hhu", rank,
		    PMIX_VAL_FIELD_flag(val));
	    break;
	default:
	    mlog("%s: unsupported value type\n", __func__);
	    *buffer = '\0';
    }

    if (res >= SBUFSIZE) {
	mlog("%s: BUFFER SIZE TOO SMALL!!!\n", __func__);
	*buffer = '\0';
    }

    return buffer;
}

/* Fill a typed pmix value from its string repesentation */
static void decodeValue(const char *encval, pmix_value_t *val,
	pmix_rank_t *rank)
{
    char buffer[SBUFSIZE];
    char *ptr;
    char type[9];
    uint8_t tmpbool;

    /* initialize to undef */
    PMIX_VALUE_CONSTRUCT(val);

    /* copy to buffer for modifications during parsing */
    strncpy(buffer, encval, SBUFSIZE-1);

    /* search first colon */
    ptr = strchr(buffer, ':');

    if (!ptr) {
	mlog("%s: wrong format encval: no colon found\n", __func__);
	/* return undef */
	return;
    }

    /* search second colon */
    ptr = strchr(++ptr, ':');

    if (!ptr) {
	mlog("%s: wrong format encval: no second colon found\n", __func__);
	/* return undef */
	return;
    }

    /* terminate rank:type string */
    *ptr='\0';

    if (sscanf(buffer, "%d:%8s", rank, type) != 2) {
	mlog("%s: wrong format encval '%s': failed to scan rank:type string"
	     " '%s'\n", __func__, encval, buffer);
	/* return undef */
	return;
    }

    /* set ptr to start of value string */
    ptr++;

    /* scan value by type */
    if (strcmp(type, "int") == 0) {
	if (sscanf(ptr, "%d", &PMIX_VAL_FIELD_int(val)) == 1) {
	    val->type = PMIX_VAL_TYPE_int;
	}
    }
    else if (strcmp(type, "uint32_t") == 0) {
	if (sscanf(ptr, "%u", &PMIX_VAL_FIELD_uint32_t(val)) == 1) {
	    val->type = PMIX_VAL_TYPE_uint32_t;
	}
    }
    else if (strcmp(type, "uint16_t") == 0) {
	if (sscanf(ptr, "%hu", &PMIX_VAL_FIELD_uint16_t(val)) == 1) {
	    val->type = PMIX_VAL_TYPE_uint16_t;
	}
    }
    else if (strcmp(type, "string") == 0) {
	PMIX_VAL_FIELD_string(val) = strndup(ptr, SBUFSIZE - 7);
	val->type = PMIX_VAL_TYPE_string;
    }
    else if (strcmp(type, "float") == 0) {
	if (sscanf(ptr, "%f", &PMIX_VAL_FIELD_float(val)) == 1) {
	    val->type = PMIX_VAL_TYPE_float;
	}
    }
    else if (strcmp(type, "byte") == 0) {
	if (sscanf(ptr, "%hhu", &PMIX_VAL_FIELD_byte(val)) == 1) {
	    val->type = PMIX_VAL_TYPE_byte;
	}
    }
    else if (strcmp(type, "flag") == 0) {
	if (sscanf(ptr, "%hhu", &tmpbool) == 1) {
	    PMIX_VAL_FIELD_flag(val) = tmpbool;
	    val->type = PMIX_VAL_TYPE_flag;
	}
    }
    else {
	mlog("%s: unknown type '%s' found in encval\n", __func__, type);
	/* return undef */
	return;
    }

    if (val->type == PMIX_UNDEF) {
	mlog("%s: could not parse value string '%s' (type '%s') in encval"
	     " '%s'\n", __func__, ptr, type, encval);
    }

    return;
}
#endif

/* Notify the host environment that a client has called PMIx_Init.
 * Note that the client will be in a blocked state until the host server
 * executes the callback function, thus allowing the PMIx server support library
 * to release the client. The server_object parameter will be the value of the
 * server_object parameter passed to PMIx_server_register_client by the
 * host server when registering the connecting client.
 * It is possible that only a subset of the clients in a namespace call
 * PMIx_Init. The server’s pmix_server_client_connected2_fn_t implementation
 * should therefore not depend on being called once per rank in a namespace or
 * delay calling the callback function until all ranks have connected. However,
 * the host may rely on the pmix_server_client_connected2_fn_t function module
 * entry being called for a given rank prior to any other function module
 * entries being executed on behalf of that rank.
 *
 * Note that this text is copied from the standard and we use our clientObject
 * as what the standard calls server_object */
/* pmix_server_client_connected2_fn_t */
#if PMIX_VERSION_MAJOR >= 4
static pmix_status_t server_client_connected2_cb(const pmix_proc_t *proc,
						 void *clientObject,
						 pmix_info_t info[],
						 size_t ninfo,
						 pmix_op_cbfunc_t cbfunc,
						 void *cbdata)
#else
static pmix_status_t server_client_connected_cb(const pmix_proc_t *proc,
						 void *clientObject,
						 pmix_op_cbfunc_t cbfunc,
						 void *cbdata)
#endif
{
    mdbg(PSPMIX_LOG_CALL,
	 "%s(proc(%d, '%s', clientObject %p cbfunc %p cbdata %p)\n", __func__,
	 proc->rank, proc->nspace, clientObject, cbfunc, cbdata);

    mycbfunc_t *cb = NULL;
    if (cbfunc) {
	cb = umalloc(sizeof(*cb));
	cb->cbfunc = cbfunc;
	cb->cbdata = cbdata;
    }

    if (!pspmix_service_clientConnected(clientObject, cb)) return PMIX_ERROR;

    /* tell the server library to wait for the callback call */
    return PMIX_SUCCESS;
}

/* Notify the host environment that a client called PMIx_Finalize.
 * Note that the client will be in a blocked state until the host server
 * executes the callback function, thus allowing the PMIx server support library
 * to release the client. The server_object parameter will be the value of the
 * server_object parameter passed to PMIx_server_register_client by the host
 * server when registering the connecting client. If provided, an implementation
 * of pmix_server_client_finalized_fn_t is only required to call the callback
 * function designated.
 * Note that the host server is only being informed that the client has called
 * PMIx_Finalize. The client might not have exited. If a client exits without
 * calling PMIx_Finalize, the server support library will not call the
 * pmix_server_client_finalized_fn_t implementation.
 *
 * Note that this text is copied from the standard and we use our clientObject
 * as what the standard calls server_object */
/* pmix_server_client_finalized_fn_t */
static pmix_status_t server_client_finalized_cb(const pmix_proc_t *proc,
	void* clientObject, pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL,
	 "%s(proc(%d, '%s') clientObject %p cbfunc %p cbdata %p)\n", __func__,
	 proc->rank, proc->nspace, clientObject, cbfunc, cbdata);

    mycbfunc_t *cb = NULL;
    if (cbfunc) {
	cb = umalloc(sizeof(*cb));
	cb->cbfunc = cbfunc;
	cb->cbdata = cbdata;
    }

    if (!pspmix_service_clientFinalized(clientObject, cb)) return PMIX_ERROR;

    /* tell the server library to wait for the callback call */
    return PMIX_SUCCESS;
}

void pspmix_server_operationFinished(bool success, void* cb)
{
    /* check if the server library does provide a callback function */
    if (!cb) return;

    mycbfunc_t *callback = cb;

    mdbg(PSPMIX_LOG_CALL, "%s(success %s cbfunc %p cbdata %p)\n", __func__,
	 success ? "true" : "false", callback->cbfunc, callback->cbdata);

    callback->cbfunc(success ? PMIX_SUCCESS : PMIX_ERROR, callback->cbdata);
}

/* A local client called PMIx_Abort.
 * Note that the client will be in a blocked state until the host server
 * executes the callback function, thus allowing the PMIx server library to
 * release the client.
 * The array of procs indicates which processes are to be terminated. A NULL for
 * the procs array indicates that all processes in the caller’s namespace are to
 * be aborted, including itself - this is the equivalent of passing a
 * pmix_proc_t array element containing the caller’s namespace and a rank
 * value of PMIX_RANK_WILDCARD. */
/* pmix_server_abort_fn_t */
static pmix_status_t server_abort_cb(const pmix_proc_t *proc,
	void *clientObject, int status, const char msg[], pmix_proc_t procs[],
	size_t nprocs, pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s(proc(%d, '%s') clientObject %p status %d nprocs"
	 " %zd cbdata %p)\n", __func__, proc->rank, proc->nspace, clientObject,
	 status, nprocs, cbdata);

    mlog("Got notification of abort request by %s:%d for ", proc->nspace,
	 proc->rank);
    if (!procs || (nprocs == 1
		   && PMIX_CHECK_NSPACE(procs[0].nspace, proc->nspace)
		   && procs[0].rank == PMIX_RANK_WILDCARD)) {
	mlog("the whole namespace\n");
    } else {
	for (size_t i = 0; i < nprocs; i++) {
	    mlog(" %s%s:%d", i ? "," : "", proc[i].nspace, proc[i].rank);
	}
	mlog(" (not supported)\n");

	// we do currently not support aborting subsets of namespaces
#if PMIX_VERSION_MAJOR < 4
	return PMIX_ERR_NOT_SUPPORTED;
#else
	return PMIX_ERR_PARAM_VALUE_NOT_SUPPORTED;
#endif
    }

    pspmix_service_abort(clientObject);

    return PMIX_OPERATION_SUCCEEDED;
}

/* free everything to be freed in fence stuff */
static void fencenb_release_fn(void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    modexdata_t *mdata = cbdata;

    ufree(mdata->data);
    memset(mdata, 0, sizeof(*mdata)); /* only for debugging */
    PMIX_PROC_DESTRUCT(&mdata->proc);
    ufree(mdata);
}

/* tell server helper library about fence finished
 *
 * This takes over ownership of @a mdata so make sure it is dynamically
 * allocated using umalloc(). Of cause the same holds for mdata->data.
 * This function uses the fields data, ndata, cbfunc, and cbdata of mdata.
 * It is fine for each of them to be NULL resp. 0, but if data is set, cbfunc
 * has to be set, too.
 */
void pspmix_server_fenceOut(bool success, modexdata_t *mdata)
{
    assert(mdata != NULL);
    assert(mdata->cbfunc != NULL || mdata->data == NULL);

    mdbg(PSPMIX_LOG_CALL, "%s(success %s, mdata->cbfunc %s,"
	 " mdata->ndata %lu)\n", __func__, success ? "true" : "false",
	 mdata->cbfunc ? "set" : "unset", mdata->ndata);

    pmix_status_t status = success ? PMIX_SUCCESS : PMIX_ERROR;

    /* Return modex data. The returned blob contains the data collected from
     * each server participating in the operation.
     * As the data is "owned" by the host server, provide a secondary callback
     * function to notify the host server that we are done with the data so it
     * can be released */
    if (mdata->cbfunc) {
	mdata->cbfunc(status, mdata->data, mdata->ndata, mdata->cbdata,
		      fencenb_release_fn, mdata);
    } else {
	assert(mdata->data == NULL);
	PMIX_PROC_DESTRUCT(&mdata->proc);
	ufree(mdata);
    }
}

/* At least one client called either PMIx_Fence or PMIx_Fence_nb. In either case,
 * the host server will be called via a non-blocking function to execute
 * the specified operation once all participating local procs have
 * contributed. All processes in the specified array are required to participate
 * in the Fence[_nb] operation. The callback is to be executed once each daemon
 * hosting at least one participant has called the host server's fencenb function.
 *
 * The provided data is to be collectively shared with all PMIx
 * servers involved in the fence operation, and returned in the modex
 * cbfunc. A _NULL_ data value indicates that the local procs had
 * no data to contribute.
 *
 * The array of info structs is used to pass user-requested options to the server.
 * This can include directives as to the algorithm to be used to execute the
 * fence operation. The directives are optional _unless_ the _mandatory_ flag
 * has been set - in such cases, the host RM is required to return an error
 * if the directive cannot be met. */
/* pmix_server_fencenb_fn_t  */
static pmix_status_t server_fencenb_cb(
	const pmix_proc_t procs[], size_t nprocs,
	const pmix_info_t info[], size_t ninfo,
	char *data, size_t ndata,
	pmix_modex_cbfunc_t cbfunc, void *cbdata)
{
    assert(procs != NULL);
    assert(nprocs != 0);

    if (mset(PSPMIX_LOG_CALL)) {
	mlog("%s(processes ", __func__);
	char rankstr[20];
	for (size_t i = 0; i < nprocs; i++) {

	    if (procs[i].rank == PMIX_RANK_WILDCARD) {
		snprintf(rankstr, sizeof(rankstr), "*");
	    } else {
		snprintf(rankstr, sizeof(rankstr), "%u", procs[i].rank);
	    }

	    if (!i) {
		mlog("%s{%s", procs[i].nspace, rankstr);
		continue;
	    }

	    /* i > 0 */
	    if (PMIX_CHECK_NSPACE(procs[i].nspace, procs[i-1].nspace)) {
		mlog(",%s", rankstr);
	    } else {
		mlog("},%s{%s", procs[i].nspace, rankstr);
	    }
	}
	mlog("})\n");
    }

    /* handle command directives */
    for (size_t i = 0; i < ninfo; i++) {

	/* This is required to be supported by PMIx 4.1 standard.
	 * Actually, I don't know, what to to with this information. We just do
	 * get blobs from each process participating in the fence and send them
	 * around, we do not control whether there are all information included
	 * or not (@todo) */
	if (PMIX_CHECK_KEY(info+i, PMIX_COLLECT_DATA)) {
	    mlog("%s: Found %s info [key '%s' value '%s']\n", __func__,
		 (PMIX_INFO_IS_REQUIRED(&info[i])) ? "required" : "optional",
		 info[i].key,
		 (PMIX_INFO_TRUE(&info[i])) ? "true" : "false");
	    continue;
	}

#if PMIX_VERSION_MAJOR >= 4
	/* This is not part of PMIx 4.1 standard but is used by OpenPMIx 4.
	 * Currently it is included as provisional in the PMIx standard HEAD */
	if (PMIX_CHECK_KEY(info+i, PMIX_LOCAL_COLLECTIVE_STATUS)) {
	    mdbg(PSPMIX_LOG_FENCE, "%s: Found %s info [key '%s' value '%s']\n",
		 __func__,
		 (PMIX_INFO_IS_REQUIRED(&info[i])) ? "required" : "optional",
		 info[i].key,
		 PMIx_Error_string(info[i].value.data.status));
	    if (info[i].value.data.status != PMIX_SUCCESS) {
		mlog("%s: Local collective error status passed: %s\n", __func__,
		     PMIx_Error_string(info[i].value.data.status));
	    }
	    continue;
	}

	/* This is not part of any PMIx standard version, but used by
	 * openPMIx since 4.2.1.
	 * https://github.com/openpmix/openpmix/commit/e66c29b93
	 * Currently we do not rely on a sorted list, perhaps there is room for
	 * optimization if this info is passed (@todo) */
	if (PMIX_CHECK_KEY(info+i, PMIX_SORTED_PROC_ARRAY)) {
	    mdbg(PSPMIX_LOG_FENCE, "%s: Found %s info [key '%s' value '%s']\n",
		 __func__,
		 (PMIX_INFO_IS_REQUIRED(&info[i])) ? "required" : "optional",
		 info[i].key,
		 (PMIX_INFO_TRUE(&info[i])) ? "true" : "false");
	    continue;
	}
#endif

	/* inform about lacking implementation */
	mlog("%s: Ignoring info [key '%s' flags '%s' value.type '%s']"
	     " (not implemented)\n", __func__, info[i].key,
	     PMIx_Info_directives_string(info[i].flags),
	     PMIx_Data_type_string(info[i].value.type));
    }

    /* initialize return data struct */
    modexdata_t *mdata = ucalloc(sizeof(*mdata));
    PMIX_PROC_CONSTRUCT(&mdata->proc);
    mdata->cbfunc = cbfunc;
    mdata->cbdata = cbdata;

    /* XXX kind of a hack, can there be multiple nspaces involved? */
    PMIX_PROC_LOAD(&mdata->proc, procs[0].nspace, 0);

    int ret;
    ret = pspmix_service_fenceIn(procs, nprocs, data, ndata, mdata);
    if (ret == -1) return PMIX_ERROR;
    if (ret == 0) return PMIX_SUCCESS;
    assert(ret == 1);

    PMIX_PROC_DESTRUCT(&mdata->proc);
    ufree(mdata->data);
    ufree(mdata);
    mlog("%s: return PMIX_OPERATION_SUCCEEDED\n", __func__);
    return PMIX_OPERATION_SUCCEEDED;
}

/* free everything to be freed in fence stuff */
static void dmodex_req_release_fn(void *cbdata)
{
    assert(cbdata != NULL);

    modexdata_t *mdata = cbdata;

    /* free data allocated by pspmix_service_handleModexDataResponse() */
    ufree(mdata->data);

    /* free struct allocated by server_dmodex_req_cb() */
    PMIX_PROC_DESTRUCT(&mdata->proc);
    strvDestroy(&mdata->reqKeys);
    ufree(mdata);
}

/* Return modex data. The returned blob contains the data from the
 * process requested got from the server of that process.
 *
 * This function takes the ownership of mdata, so no locking required */
void pspmix_server_returnModexData(pmix_status_t status, modexdata_t *mdata)
{
    assert(mdata != NULL);

    mdbg(PSPMIX_LOG_CALL, "%s(status %s rank %u namespace %s ndata %lu)\n",
	 __func__, PMIx_Error_string(status), mdata->proc.rank,
	 mdata->proc.nspace, mdata->ndata);

    /* Call the callback provided by the server in server_dmodex_req_cb().
     * As the data is "owned" by the host server, provide a secondary callback
     * function to notify the host server that we are done with the data so it
     * can be released */
    if (mdata->cbfunc) {
	mdata->cbfunc(status, mdata->data, mdata->ndata, mdata->cbdata,
		      dmodex_req_release_fn, mdata);
    }
}

/* Used by the PMIx server to request its local host contact the PMIx server on
 * the remote node that hosts the specified proc to obtain and return any
 * information that process posted via calls to PMIx_Put and PMIx_Commit.
 *
 * The array of info structs is used to pass user-requested options to the
 * server. This can include a timeout to preclude an indefinite wait for data
 * that may never become available. The directives are optional unless the
 * mandatory flag has been set - in such cases, the host RM is required to
 * return an error if the directive cannot be met. */
/* pmix_server_dmodex_req_fn_t */
static pmix_status_t server_dmodex_req_cb(const pmix_proc_t *proc,
					  const pmix_info_t info[],
					  size_t ninfo,
					  pmix_modex_cbfunc_t cbfunc,
					  void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s(rank %u namespace %s)\n", __func__, proc->rank,
	 proc->nspace);

    strv_t reqKeys;
    char *emptyStr = NULL;
    strvInit(&reqKeys, &emptyStr, 0); // ensure reqKeys.strings get initialized
    int timeout = 0;

    /* handle command directives */
    for (size_t i = 0; i < ninfo; i++) {

#if PMIX_VERSION_MAJOR >= 4
	/* debug print each info */
	mdbg(PSPMIX_LOG_MODEX, "%s: Found %s info [key '%s' flags '%s'"
	     " value.type '%s'\n", __func__,
	     (PMIX_INFO_IS_REQUIRED(info+i)) ? "required" : "optional",
	     info[i].key, PMIx_Info_directives_string(info[i].flags),
	     PMIx_Data_type_string(info[i].value.type));
	/* @todo use PMIx_Info_string() for PMIx > 4.1.2 */

	/* support mendatory key PMIX_REQUIRED_KEY */
	if (PMIX_CHECK_KEY(info+i, PMIX_REQUIRED_KEY)) {
	    strvAdd(&reqKeys, ustrdup(info[i].value.data.string));
	    continue;
	}

	/* support optional key PMIX_TIMEOUT */
	if (PMIX_CHECK_KEY(info+i, PMIX_TIMEOUT)) {
	    timeout = info[i].value.data.integer;
	    continue;
	}

	/* ignore keys not relevant for our implementation */
	if (PMIX_CHECK_KEY(info+i, PMIX_GET_REFRESH_CACHE)) {
	    /* we no not manage an own modex cache */
	    continue;
	}

	/* info with required directive are not allowed to be ignored */
	if (PMIX_INFO_IS_REQUIRED(info+i)) {
	    mlog("%s: Error: Unsupported info [key '%s' flags '%s' value.type"
		 " '%s'] marked required", __func__, info[i].key,
		 PMIx_Info_directives_string(info[i].flags),
		 PMIx_Data_type_string(info[i].value.type));
	    strvDestroy(&reqKeys);
	    return PMIX_ERR_NOT_SUPPORTED;
	}
#endif

	/* inform about lacking implementation */
	mlog("%s: Ignoring info [key '%s' flags '%s' value.type '%s']"
	     " (not implemented)\n", __func__, info[i].key,
	     PMIx_Info_directives_string(info[i].flags),
	     PMIx_Data_type_string(info[i].value.type));
    }

    /* initialize return data struct */
    modexdata_t *mdata = ucalloc(sizeof(*mdata));
    PMIX_PROC_CONSTRUCT(&mdata->proc);
    PMIX_PROC_LOAD(&mdata->proc, proc->nspace, proc->rank);
    mdata->cbfunc = cbfunc;
    mdata->cbdata = cbdata;
    mdata->reqKeys = reqKeys;
    mdata->timeout = timeout;

    if (!pspmix_service_sendModexDataRequest(mdata)) {
	mlog("%s: pspmix_service_sendModexDataRequest() for rank %u in"
	     " namespace %s failed.\n", __func__, proc->rank, proc->nspace);
	PMIX_PROC_DESTRUCT(&mdata->proc);
	strvDestroy(&reqKeys);
	ufree(mdata);

	return PMIX_ERROR;
    }

    return PMIX_SUCCESS;
}

/*
 * Function called by the PMIx server to return direct modex data
 * requests to the host server. The PMIx server will free the data blob
 * upon return from this function
 */
static void requestModexData_cb(pmix_status_t status, char *data, size_t ndata,
				void *cbdata)
{
    modexdata_t *mdata = cbdata;

    mdbg(PSPMIX_LOG_CALL, "%s(ndata %zu rank %u namespace %s)\n", __func__,
	 ndata, mdata->proc.rank, mdata->proc.nspace);

    mdata->data = data;
    mdata->ndata = ndata;

    if (status != PMIX_SUCCESS) {
	mlog("%s: modex data request for rank %u in namespace %s failed:"
	     " %s\n", __func__, mdata->proc.rank, mdata->proc.nspace,
	     PMIx_Error_string(status));
    }

    pspmix_service_sendModexDataResponse(status, mdata);
}

/**
 * @brief Check for availability of all required keys at a client
 *
 * @param proc     client process
 * @param reqKeys  array of keys (NULL terminated)
 */
static bool checkKeyAvailability(pmix_proc_t *proc, strv_t *reqKeys)
{
    if (!reqKeys->strings) return true;

#if PMIX_VERSION_MAJOR < 4
    size_t ninfo = 1;
#else
    /* @todo move to PMIX_INFO_LIST_* macro when removing PMIx < 4 support */
    size_t ninfo = 2;
#endif
    pmix_info_t *info;
    PMIX_INFO_CREATE(info, ninfo);

    size_t i = 0;
    bool flag = true;
    PMIX_INFO_LOAD(&info[i], PMIX_IMMEDIATE, &flag, PMIX_BOOL);
    i++;

#if PMIX_VERSION_MAJOR >= 4
    PMIX_INFO_LOAD(&info[i], PMIX_GET_POINTER_VALUES, &flag, PMIX_BOOL);
    i++;
#endif

    size_t c = 0;
    char *key;
    while ((key = reqKeys->strings[c++])) {
	pmix_value_t *val;
	pmix_status_t status = PMIx_Get(proc, key, info, ninfo, &val);
	switch (status) {
	    case PMIX_SUCCESS:
		udbg(PSPMIX_LOG_MODEX, "found '%s' for rank %d\n", key,
		     proc->rank);
#if PMIX_VERSION_MAJOR < 4
		PMIX_VALUE_DESTRUCT(val);
#endif
		break;
	    case PMIX_ERR_NOT_FOUND:
		udbg(PSPMIX_LOG_MODEX, "not found '%s' for rank %d\n", key,
		     proc->rank);
		PMIX_INFO_FREE(info, ninfo);
		return false;
	    case PMIX_ERR_BAD_PARAM:
#if PMIX_VERSION_MAJOR >= 4
	    case PMIX_ERR_EXISTS_OUTSIDE_SCOPE:
#endif
		mlog("%s: PMIx_get(proc %s:%d key %s) failed: %s\n", __func__,
			proc->nspace, proc->rank, key,
			PMIx_Error_string(status));
		PMIX_INFO_FREE(info, ninfo);
		return false;
	}
    }
    PMIX_INFO_FREE(info, ninfo);
    return true;
}


/**
 * @brief Timeout handler
 *
 * Handler called upon expiration of the timeout associated to the
 * script or function to execute identified by @a info. This will try
 * to cleanup all used resources and call the callback accordingly.
 *
 * @param timerID ID of the expired timer
 *
 * @param info Information blob identifying the associated script/func
 *
 * @return No return value
 */
static void reqModexTimeoutHandler(int timerID, void *info)
{
    modexdata_t *mdata = info;
    if (!mdata) {
	mlog("%s: no mdata for %d\n", __func__, timerID);
	return;
    }

    if (checkKeyAvailability(&mdata->proc, &mdata->reqKeys)) {
	Timer_remove(timerID);
	/* there are either no keys required or all available */
	pmix_status_t status =
		PMIx_server_dmodex_request(&mdata->proc, requestModexData_cb,
					   mdata);
	if (status != PMIX_SUCCESS) {
	    mlog("%s: PMIx_server_dmodex_request() failed: %s\n", __func__,
		 PMIx_Error_string(status));
	}
	return;
    }

    udbg(PSPMIX_LOG_MODEX, "not yet found all required keys for rank %d\n",
	 mdata->proc.rank);

    time_t curtime = time(NULL);
    if (curtime - mdata->reqtime > mdata->timeout) {
	/* time is over */
	Timer_remove(timerID);
	requestModexData_cb(PMIX_ERR_TIMEOUT, NULL, 0, mdata);
	/* @todo should we send all available data nevertheless? */
    }
}

/* Request modex data from the local PMIx server. This is used to support
 * the direct modex operation - i.e., where data is cached locally on each
 * PMIx server for its own local clients, and is obtained on-demand for
 * remote requests. Upon receiving a request from a remote server, the host
 * server will call this function to pass the request into the PMIx server.
 * The PMIx server will return a blob (once it becomes available) via the
 * cbfunc @a requestModexData_cb() - the host server shall send the blob back
 * to the original requestor there passing to @a server_returnModexData()
 */
bool pspmix_server_requestModexData(modexdata_t *mdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s(rank %u namespace %s)\n", __func__,
	 mdata->proc.rank, mdata->proc.nspace);

    /* store time processing of the request has started */
    mdata->reqtime = time(NULL);

    if (checkKeyAvailability(&mdata->proc, &mdata->reqKeys)) {
	/* there are either no keys required or all available */
	pmix_status_t status =
		PMIx_server_dmodex_request(&mdata->proc, requestModexData_cb,
					   mdata);
	if (status != PMIX_SUCCESS) {
	    mlog("%s: PMIx_server_dmodex_request() failed: %s\n", __func__,
		 PMIx_Error_string(status));
	    return false;
	}

	return true;
    }

    /* lookup for availability of required keys every second until timeout
     *  @todo frequency is a subject to be discussed */
    struct timeval polltime = (struct timeval) { 0, 500000 };
    Timer_registerEnhanced(&polltime, reqModexTimeoutHandler, mdata);

    return true;
}

/* Publish data per the PMIx_Publish specification.
 * The callback is to be executed upon completion of the operation. The default
 * data range is left to the host environment, but expected to be
 * PMIX_RANGE_SESSION, and the default persistence PMIX_PERSIST_SESSION or their
 * equivalent. These values can be specified by including the respective
 * attributed in the info array. The persistence indicates how long the server
 * should retain the data.
 *
 * Advice to PMIx server hosts:
 * The host environment is not required to guarantee support for any specific
 * range - i.e., the environment does not need to return an error if the data
 * store doesn’t support a specified range so long as it is covered by some
 * internally defined range. However, the server must return an error
 * (a) if the key is duplicative within the storage range, and
 * (b) if the server does not allow overwriting of published info by the
 * original publisher - it is left to the discretion of the host environment to
 * allow info-key-based flags to modify this behavior.
 *
 * The PMIX_USERID and PMIX_GRPID of the publishing process will be provided to
 * support authorization-based access to published information and must be
 * returned on any subsequent lookup request. */
/* pmix_server_publish_fn_t */
static pmix_status_t server_publish_cb(const pmix_proc_t *proc,
				       const pmix_info_t info[], size_t ninfo,
				       pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    // @todo implement
#if 0
    size_t errcount = 0;
    for (size_t i = 0; i < ninfo; i++) {
	const char *encval = encodeValue(&(info[i].value), proc->rank);
	bool ret = pspmix_service_putToKVS(proc->nspace, info[i].key, encval);
	if (!ret) errcount++;

	/* inform about lacking implementation */
	mlog("%s: Ignoring flags for key '%s' flags '%s' (not implemented)\n",
	     __func__, info[i].key, PMIx_Info_directives_string(info[i].flags));
    }

    if (cbfunc) cbfunc(errcount == 0 ? PMIX_SUCCESS : PMIX_ERROR, cbdata);

    return PMIX_SUCCESS;
#endif

    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Lookup published data. The host server will be passed a NULL-terminated array
 * of string keys identifying the data being requested.
 * The array of info structs is used to pass user-requested options to the
 * server. The default data range is left to the host environment, but expected
 * to be PMIX_RANGE_SESSION. This can include a wait flag to indicate that the
 * server should wait for all data to become available before executing the
 * callback function, or should immediately callback with whatever data is
 * available. In addition, a timeout can be specified on the wait to preclude an
 * indefinite wait for data that may never be published.
 *
 * Advice to PMIx server hosts:
 * The PMIX_USERID and PMIX_GRPID of the requesting process will be provided to
 * support authorization-based access to published information. The host
 * environment is not required to guarantee support for any specific range -
 * i.e., the environment does not need to return an error if the data store
 * doesn’t support a specified range so long as it is covered by some internally
 * defined range. */
/* pmix_server_lookup_fn_t */
static pmix_status_t server_lookup_cb(const pmix_proc_t *proc, char **keys,
				      const pmix_info_t info[], size_t ninfo,
				      pmix_lookup_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* @todo implement */
#if 0
    const char *encval;

    /* inform about lacking implementation */
    for (size_t i = 0; i < ninfo; i++) {
	mlog("%s: Ignoring info [key '%s' flags '%s' value.type '%s']"
	     " (not implemented)\n", __func__, info[i].key,
	     PMIx_Info_directives_string(info[i].flags),
	     PMIx_Data_type_string(info[i].value.type));
    }

    /* count keys */
    size_t nkeys = 0;
    while(keys[nkeys]) nkeys++;

    /* create array of return data */
    pmix_pdata_t *data;
    PMIX_PDATA_CREATE(data, nkeys);

    size_t ndata = 0;
    for (size_t i = 0; i < nkeys; i++) {
	/* get value from KVS */
	encval = pspmix_service_getFromKVS(proc->nspace, keys[i]);

	if (!encval) {
	    mlog("%s: Error getting key '%s' in namespace '%s'\n", __func__,
		 keys[i], proc->nspace);
	    continue;
	}

	/* fill pdata */
	memset(&data[ndata], 0, sizeof(pmix_pdata_t));
	strncpy(data[ndata].proc.nspace, proc->nspace, PMIX_MAX_NSLEN+1);
	strncpy(data[ndata].key, keys[i], PMIX_MAX_KEYLEN);
	decodeValue(encval, &(data[ndata].value), &(data[ndata].proc.rank));
    }

    if (ndata < nkeys) {
	data = realloc(data, ndata * sizeof(pmix_pdata_t));
    }

    if (cbfunc) cbfunc(PMIX_SUCCESS, data, nkeys, cbdata);

    return PMIX_SUCCESS;
#endif

    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Delete data from the data store. The host server will be passed a
 * NULL-terminated array of string keys, plus potential directives such as the
 * data range within which the keys should be deleted. The default data range is
 * left to the host environment, but expected to be PMIX_RANGE_SESSION.
 * The callback is to be executed upon completion of the delete procedure.
 *
 * Advice to PMIx server hosts:
 * The PMIX_USERID and PMIX_GRPID of the requesting process will be provided to
 * support authorization-based access to published information. The host
 * environment is not required to guarantee support for any specific range -
 * i.e., the environment does not need to return an error if the data store
 * doesn’t support a specified range so long as it is covered by some internally
 * defined range. */
/* pmix_server_unpublish_fn_t */
static pmix_status_t server_unpublish_cb(const pmix_proc_t *proc, char **keys,
					 const pmix_info_t info[], size_t ninfo,
					 pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    // @todo implement

    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Spawn a set of applications/processes as per the PMIx_Spawn API.
 * Note that applications are not required to be MPI or any other programming
 * model. Thus, the host server cannot make any assumptions as to their required
 * support. The callback function is to be executed once all processes have been
 * started. An error in starting any application or process in this request
 * shall cause all applications and processes in the request to be terminated,
 * and an error returned to the originating caller.
 * Note that a timeout can be specified in the job_info array to indicate that
 * failure to start the requested job within the given time should result in
 * termination to avoid hangs. */
/* pmix_server_spawn_fn_t  */
static pmix_status_t server_spawn_cb(const pmix_proc_t *proc,
				     const pmix_info_t job_info[], size_t ninfo,
				     const pmix_app_t apps[], size_t napps,
				     pmix_spawn_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    // @todo implement at highest priority

    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Record the processes specified by the procs array as connected as per the
 * PMIx definition. The callback is to be executed once every daemon hosting at
 * least one participant has called the host server’s pmix_server_connect_fn_t
 * function, and the host environment has completed any supporting operations
 * required to meet the terms of the PMIx definition of connected processes.
 *
 * Advice to PMIx server hosts:
 * The host will receive a single call for each collective operation. It is the
 * responsibility of the host to identify the nodes containing participating
 * processes, execute the collective across all participating nodes, and notify
 * the local PMIx server library upon completion of the global collective. */
/* pmix_server_connect_fn_t */
static pmix_status_t server_connect_cb(const pmix_proc_t procs[], size_t nprocs,
				       const pmix_info_t info[], size_t ninfo,
				       pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    // @todo implement

    /* not implemented yet */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Disconnect a previously connected set of processes. The callback is to be
 * executed once every daemon hosting at least one participant has called the
 * host server’s has called the pmix_server_disconnect_fn_t function, and the
 * host environment has completed any required supporting operations.
 *
 * Advice to PMIx server hosts:
 * The host will receive a single call for each collective operation. It is the
 * responsibility of the host to identify the nodes containing participating
 * processes, execute the collective across all participating nodes, and notify
 * the local PMIx server library upon completion of the global collective.
 * A PMIX_ERR_INVALID_OPERATION error must be returned if the specified set of
 * procs was not previously connected via a call to the pmix_server_connect_fn_t
 * function. */
/* pmix_server_disconnect_fn_t */
static pmix_status_t server_disconnect_cb(const pmix_proc_t procs[], size_t nprocs,
					  const pmix_info_t info[], size_t ninfo,
					  pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    // @todo implement

    /* not implemented yet */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Register to receive notifications for the specified status codes. The info
 * array included in this API is reserved for possible future directives to
 * further steer notification.
 *
 * Advice to PMIx server hosts:
 * The host environment is required to pass to its PMIx server library all
 * non-environmental events that directly relate to a registered namespace
 * without the PMIx server library explicitly requesting them. Environmental
 * events are to be translated to their nearest PMIx equivalent code as defined
 * in the range between PMIX_EVENT_SYS_BASE and PMIX_EVENT_SYS_OTHER
 * (inclusive). */
/* pmix_server_register_events_fn_t */
static pmix_status_t server_register_events_cb(pmix_status_t *codes, size_t ncodes,
					       const pmix_info_t info[], size_t ninfo,
					       pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    // @todo implement

    /* not implemented yet */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Deregister to receive notifications for the specified events to which the
 * PMIx server has previously registered. */
/* pmix_server_deregister_events_fn_t */
static pmix_status_t server_deregister_events_cb(pmix_status_t *codes, size_t ncodes,
						 pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    // @todo implement

    /* not implemented yet */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Notify the specified processes (described through a combination of range
 * and attributes provided in the info array) of an event generated either by
 * the PMIx server itself or by one of its local clients.
 *
 * The process generating the event is provided in the source parameter, and any
 * further descriptive information is included in the info array.
 *
 * Note that the PMIx server library is not allowed to echo any event given to
 * it by its host via the PMIx_Notify_event API back to the host through the
 * pmix_server_notify_event_fn_t server module function.
 *
 * Advice to PMIx server hosts:
 * The callback function is to be executed once the host environment no longer
 * requires that the PMIx server library maintain the provided data structures.
 * It does not necessarily indicate that the event has been delivered to any
 * process, nor that the event has been distributed for delivery */
/* pmix_server_notify_event_fn_t */
static pmix_status_t server_notify_event_cb(pmix_status_t code,
					    const pmix_proc_t *source,
					    pmix_data_range_t range,
					    pmix_info_t info[], size_t ninfo,
					    pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    // @todo implement

    /* not implemented */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

#if 0
/* Register a socket the host environment can monitor for connection requests,
 * harvest them, and then call the PMIx server library’s internal callback
 * function for further processing. A listener thread is essential to
 * efficiently harvesting connection requests from large numbers of local
 * clients such as occur when running on large SMPs. The host server listener is
 * required to call accept on the incoming connection request, and then pass the
 * resulting socket to the provided cbfunc. A NULL for this function will cause
 * the internal PMIx server to spawn its own listener thread. */
/* pmix_server_listener_fn_t */
static pmix_status_t
pspmix_server_listener_cb(int listening_sd,
			  pmix_connection_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    /* not implemented */
    return PMIX_ERR_NOT_IMPLEMENTED;
}
#endif

/* Query information from the host environment. The query will include the
 * namespace/rank of the process that is requesting the info, an array of
 * pmix_query_t describing the request, and a callback function/data for the
 * return. */
/* pmix_server_query_fn_t */
static pmix_status_t server_query_cb(pmix_proc_t *proc,
				     pmix_query_t *queries, size_t nqueries,
				     pmix_info_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s(rank %u namespace %s)\n", __func__, proc->rank,
	 proc->nspace);

    // @todo implement

    vector_t query;
    charvInit(&query, 50);
    char buf[50];
    for (size_t i = 0; i < nqueries; i++) {
	snprintf(buf, sizeof(buf), "Query %zu:", i);
	charvAddCount(&query, buf, strlen(buf));

	for (size_t j = 0; queries[i].keys[j] != NULL; j++) {
	    snprintf(buf, sizeof(buf), " Key '%s'", queries[i].keys[j]);
	    charvAddCount(&query, buf, strlen(buf));
	}
	charvAddCount(&query, "  (NOT IMPLEMENTED)\n\0", 21);
    }

    mlog("%s: %s", __func__, (char *)query.data);
    charvDestroy(&query);

    /* not implemented */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Register that a tool has connected to the server, possibly requesting that
 * the tool be assigned a namespace/rank identifier for further interactions.
 * The pmix_info_t array is used to pass qualifiers for the connection request,
 * including the effective uid and gid of the calling tool for authentication
 * purposes.
 *
 * If the tool already has an assigned process identifier, then this must be
 * indicated in the info array. The host is responsible for checking that the
 * provided namespace does not conflict with any currently known assignments,
 * returning an appropriate error in the callback function if a conflict is
 * found.
 * The host environment is solely responsible for authenticating and authorizing
 * the connection using whatever means it deems appropriate. If certificates or
 * other authentication information are required, then the tool must provide
 * them. The conclusion of those operations shall be communicated back to the
 * PMIx server library via the callback function.
 *
 * Approval or rejection of the connection request shall be returned in the
 * status parameter of the pmix_tool_connection_cbfunc_t. If the connection is
 * refused, the PMIx server library must terminate the connection attempt. The
 * host must not execute the callback function prior to returning from the API.
 * */
/* pmix_server_tool_connection_fn_t */
static void server_tool_connection_cb(pmix_info_t *info, size_t ninfo,
				      pmix_tool_connection_cbfunc_t cbfunc,
				      void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    // @todo implement at low priority

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* not implemented */
}

/* Log data on behalf of a client.
 * This function is not intended for output of computational results, but rather
 * for reporting status and error messages. The host must not execute the
 * callback function prior to returning from the API. */
/* pmix_server_log_fn_t */
static void server_log_cb(const pmix_proc_t *client,
			  const pmix_info_t data[], size_t ndata,
			  const pmix_info_t directives[], size_t ndirs,
			  pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    // @todo implement

    mlog("%s: NOT IMPLEMENTED\n", __func__);
}

/* Request new allocation or modifications to an existing allocation on behalf
 * of a client.
 *
 * Several broad categories are envisioned, including the ability to:
 * - Request allocation of additional resources, including memory, bandwidth,
 *   and compute for an existing allocation. Any additional allocated resources
 *   will be considered as part of the current allocation, and thus will be
 *   released at the same time.
 * - Request a new allocation of resources. Note that the new allocation will be
 *   disjoint from (i.e., not affiliated with) the allocation of the requestor
 *    - thus the termination of one allocation will not impact the other.
 * - Extend the reservation on currently allocated resources, subject to
 *   scheduling availability and priorities.
 * - Return no-longer-required resources to the scheduler. This includes the
 *   loan of resources back to the scheduler with a promise to return them upon
 *   subsequent request.
 *
 * The callback function provides a status to indicate whether or not the
 * request was granted, and to provide some information as to the reason for any
 * denial in the pmix_info_cbfunc_t array of pmix_info_t structures. */
/* pmix_server_alloc_fn_t */
static pmix_status_t server_alloc_cb(const pmix_proc_t *client,
				     pmix_alloc_directive_t directive,
				     const pmix_info_t data[], size_t ndata,
				     pmix_info_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    // @todo implement at low priority

    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Execute a job control action on behalf of a client.
 *
 * The targets array identifies the processes to which the requested job control
 * action is to be applied. A NULL value can be used to indicate all processes
 * in the caller’s namespace. The use of PMIX_RANK_WILDCARD can also be used to
 * indicate that all processes in the given namespace are to be included.
 *
 * The directives are provided as pmix_info_t structures in the directives
 * array. The callback function provides a status to indicate whether or not the
 * request was granted, and to provide some information as to the reason for any
 * denial in the pmix_info_cbfunc_t array of pmix_info_t structures. */
/* pmix_server_job_control_fn_t */
static pmix_status_t server_job_control_cb(const pmix_proc_t *requestor,
					   const pmix_proc_t targets[],
					   size_t ntargets,
					   const pmix_info_t directives[],
					   size_t ndirs,
					   pmix_info_cbfunc_t cbfunc,
					   void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    // @todo implement at low priority

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* not implemented */
    return PMIX_ERR_NOT_IMPLEMENTED;
}


/* Request that a client be monitored for activity */
/* pmix_server_monitor_fn_t */
static pmix_status_t server_monitor_cb(const pmix_proc_t *requestor,
				       const pmix_info_t *monitor,
				       pmix_status_t error,
				       const pmix_info_t directives[],
				       size_t ndirs,
				       pmix_info_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    // @todo implement at low priority

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* not implemented */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Request a credential from the host environment. */
/* pmix_server_get_cred_fn_t */
static pmix_status_t server_get_cred_cb(const pmix_proc_t *proc,
					const pmix_info_t directives[],
					size_t ndirs,
					pmix_credential_cbfunc_t cbfunc,
					void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    // @todo implement at low priority

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* not implemented */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Request validation of a credential. */
/* pmix_server_validate_cred_fn_t */
static pmix_status_t server_validate_cred_cb(const pmix_proc_t *proc,
					     const pmix_byte_object_t *cred,
					     const pmix_info_t directives[],
					     size_t ndirs,
					     pmix_validation_cbfunc_t cbfunc,
					     void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    // @todo implement at low priority

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* not implemented */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Request the specified IO channels be forwarded from the given array of
 * processes */
/* pmix_server_iof_fn_t */
static pmix_status_t server_iof_cb(const pmix_proc_t procs[], size_t nprocs,
				   const pmix_info_t directives[], size_t ndirs,
				   pmix_iof_channel_t channels,
				   pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    // @todo implement at low priority

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* not implemented */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Pass standard input data to the host environment for transmission to
 * specified recipients. */
/* pmix_server_stdin_fn_t */
static pmix_status_t server_stdin_cb(const pmix_proc_t *source,
				     const pmix_proc_t targets[], size_t ntargets,
				     const pmix_info_t directives[], size_t ndirs,
				     const pmix_byte_object_t *bo,
				     pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    // @todo implement at low priority

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* not implemented */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

#if PMIX_VERSION_MAJOR >= 4
/* Request group operations (construct, destruct, etc.) on behalf of a set of
 * processes.
 *
 * Perform the specified operation across the identified processes, plus any
 * special actions included in the directives. Return the result of any special
 * action requests in the callback function when the operation is completed.
 * Actions may include a request (PMIX_GROUP_ASSIGN_CONTEXT_ID) that the host
 * assign a unique numerical (size_t) ID to this group - if given, the
 * PMIX_RANGE attribute will specify the range across which the ID must be
 * unique (default to PMIX_RANGE_SESSION). */
/* pmix_server_grp_fn_t */
static pmix_status_t server_grp_cb(pmix_group_operation_t op, char grp[],
				   const pmix_proc_t procs[], size_t nprocs,
				   const pmix_info_t directives[], size_t ndirs,
				   pmix_info_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    // @todo implement at low priority

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* not implemented */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Request fabric-related operations (e.g., information on a fabric) on behalf
 * of a tool or other process.
 *
 * Perform the specified operation. Return the result of any requests in the
 * callback function when the operation is completed. Operations may, for
 * example, include a request for fabric information. See pmix_fabric_t for a
 * list of expected information to be included in the response. Note that
 * requests for device index are to be returned in the callback function’s array
 * of pmix_info_t using the PMIX_FABRIC_DEVICE_INDEX attribute. */
/* pmix_server_fabric_fn_t */
static pmix_status_t server_fabric_cb(const pmix_proc_t *requestor,
				      pmix_fabric_operation_t op,
				      const pmix_info_t directives[], size_t ndirs,
				      pmix_info_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    // @todo implement at low priority

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* not implemented */
    return PMIX_ERR_NOT_IMPLEMENTED;
}
#endif


/* struct holding the server callback functions */
static pmix_server_module_t module = {
    /* v1x interfaces */
#if PMIX_VERSION_MAJOR >= 4
    .client_connected = NULL, /* deprecated */
#else
    .client_connected = server_client_connected_cb,
#endif
    .client_finalized = server_client_finalized_cb,
    .abort = server_abort_cb,
    .fence_nb = server_fencenb_cb,
    .direct_modex = server_dmodex_req_cb,
    .publish = server_publish_cb,
    .lookup = server_lookup_cb,
    .unpublish = server_unpublish_cb,
    .spawn = server_spawn_cb,
    .connect = server_connect_cb,
    .disconnect = server_disconnect_cb,
    .register_events = server_register_events_cb,
    .deregister_events = server_deregister_events_cb,
    .listener = NULL,
#if 0
    .listener = pspmix_server_listener_cb,
#endif

    /* v2x interfaces */
    .notify_event = server_notify_event_cb,
    .query = server_query_cb,
    .tool_connected = server_tool_connection_cb,
    .log = server_log_cb,
    .allocate = server_alloc_cb,
    .job_control = server_job_control_cb,
    .monitor = server_monitor_cb,
    /* v3x interfaces */
    .get_credential = server_get_cred_cb,
    .validate_credential = server_validate_cred_cb,
    .iof_pull = server_iof_cb,
    .push_stdin = server_stdin_cb,
    /* v4x interfaces */
#if PMIX_VERSION_MAJOR >= 4
    .group = server_grp_cb,
    .fabric = server_fabric_cb,
    .client_connected2 = server_client_connected2_cb,
#endif
};

/* XXX */
static void errhandler(size_t evhdlr_registration_id, pmix_status_t status,
		       const pmix_proc_t *source,
		       pmix_info_t info[], size_t ninfo,
		       pmix_info_t results[], size_t nresults,
		       pmix_event_notification_cbfunc_fn_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s(status %d proc %s:%u ninfo %lu nresults %lu\n",
	 __func__, status, source->nspace, source->rank, ninfo, nresults);
}

#if PMIX_VERSION_MAJOR >= 4

#define INFO_LIST_ADD(i, key, val, t) \
    do { \
	pmix_status_t status = PMIx_Info_list_add(i, key, val, t); \
	if (status != PMIX_SUCCESS) mlog("%s: Failed to add : %s\n", \
					   __func__, \
					   PMIx_Error_string(status)); \
    } while(0)

static bool fillServerSessionArray(pmix_data_array_t *sessionInfo,
				   const char *clusterid)
{
    void *list = PMIx_Info_list_start();

    /* A string name for the cluster this allocation is on */
    INFO_LIST_ADD(list, PMIX_CLUSTER_ID, clusterid, PMIX_STRING);

    /* session id of UINT32_MAX means every session */
    uint32_t session_id = UINT32_MAX;
    INFO_LIST_ADD(list, PMIX_SESSION_ID, &session_id, PMIX_UINT32);

    /* String name of the RM */
    char *rmname = "ParaStation";
    INFO_LIST_ADD(list, PMIX_RM_NAME, rmname, PMIX_STRING);

    /* RM version string */
    const char *rmversion = PSC_getVersionStr();
    INFO_LIST_ADD(list, PMIX_RM_VERSION, rmversion, PMIX_STRING);

    /* Host where target PMIx server is located */
    const char *hostname = getHostnameByNodeId(PSC_getMyID());
    INFO_LIST_ADD(list, PMIX_SERVER_HOSTNAME, hostname, PMIX_STRING);

    pmix_status_t status;
    status = PMIx_Info_list_convert(list, sessionInfo);
    PMIx_Info_list_release(list);
    if (status != PMIX_SUCCESS) {
	mlog("%s: Converting info list to array failed: %s\n", __func__,
	       PMIx_Error_string(status));
	return false;
    }

    return true;
}

/**
 * To be called by PMIx_server_register_resources() to provide status
 */
static void registerResources_cb(pmix_status_t status, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mycbdata_t *data = cbdata;

    data->status = status;

    SET_CBDATA_AVAIL(data);
}
#endif

/**
 * To be called by error handler registration function to provide success state
 */
static void registerErrorHandler_cb (pmix_status_t status,
				     size_t errhandler_ref, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mycbdata_t *data = cbdata;

    data->status = status;

    SET_CBDATA_AVAIL(data);
}

bool pspmix_server_init(char *nspace, pmix_rank_t rank, const char *clusterid,
			const char *srvtmpdir, const char *systmpdir)
{
    mdbg(PSPMIX_LOG_CALL, "%s(nspace %s rank %d srvtmpdir %s systmpdir %s)\n",
	    __func__, nspace, rank, srvtmpdir, systmpdir);

    /* print some interesting environment variables if set */
    for (size_t i = 0; environ[i]; i++) {
	if (!strncmp(environ[i], "PMIX_DEBUG", 10)
	    || !strncmp (environ[i], "PMIX_OUTPUT", 11)
	    || !strncmp (environ[i], "PMIX_MCA_", 9)) {
	    mlog("%s: %s set\n", __func__, environ[i]);
	}
    }

#if PMIX_VERSION_MAJOR >= 4
    size_t ninfo = 7;
#else
    size_t ninfo = 4;
#endif
    if (srvtmpdir) ninfo++;
    if (systmpdir) ninfo++;

    mycbdata_t cbdata;
    INIT_CBDATA(cbdata, ninfo);

    size_t i = 0;
    /* Name of the namespace to use for this PMIx server */
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_SERVER_NSPACE, nspace, PMIX_STRING);
    i++;

    /* Rank of this PMIx server */
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_SERVER_RANK, &rank, PMIX_PROC_RANK);
    i++;

    /* Top-level temporary directory for all client processes connected to this
     * server, and where the PMIx server will place its tool rendezvous point
     * and contact information. */
    if (srvtmpdir) {
	PMIX_INFO_LOAD(&cbdata.info[i], PMIX_SERVER_TMPDIR, srvtmpdir,
		       PMIX_STRING);
	i++;
    }

    /* Temporary directory for this system, and where a PMIx server that
     * declares itself to be a system-level server will place a tool rendezvous
     * point and contact information. */
    if (systmpdir) {
	PMIX_INFO_LOAD(&cbdata.info[i], PMIX_SYSTEM_TMPDIR, systmpdir,
		       PMIX_STRING);
	i++;
    }

    /* The host RM wants to declare itself as willing to accept tool connection
     * requests. */
    bool tmpbool = false;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_SERVER_TOOL_SUPPORT, &tmpbool,
		   PMIX_BOOL);
    i++;

    /* The host RM wants to declare itself as being the local system server for
     * PMIx connection @todo set to true?
     *
     * PMIx servers that are designated as system servers by including the
     * PMIX_SERVER_SYSTEM_SUPPORT attribute when calling PMIx_server_init will
     * create a rendezvous file in PMIX_SYSTEM_TMPDIR top-level directory.
     * The filename will be of the form pmix.sys.hostname, where hostname is the
     * string returned by the gethostname system call. Note that only one PMIx
     * server on a node can be designated as the system server.
     *
     * Non-system PMIx servers will create a set of three rendezvous files in
     * the directory defined by either the PMIX_SERVER_TMPDIR attribute or the
     * TMPDIR environmental variable:
     * • pmix.host.tool.nspace where host is the string returned by the
     *			       gethostname system call and nspace is the
     *			       namespace of the server.
     * • pmix.host.tool.pid    where host is the string returned by the
     *			       gethostname system call and pid is the PID of the
     *			       server.
     * • pmix.host.tool        where host is the string returned by the
     *			       gethostname system call. Note that servers which
     *			       are not given a namespace-specific
     *			       PMIX_SERVER_TMPDIR attribute may not
     *			       generate this file due to conflicts should
     *			       multiple servers be present on the node.
     */
    tmpbool = false;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_SERVER_SYSTEM_SUPPORT, &tmpbool,
		   PMIX_BOOL);
    i++;

#if PMIX_VERSION_MAJOR >= 4
    /* The host RM wants to declare itself as being the local session server for
     * PMIx connection requests. */
    tmpbool = false;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_SERVER_SESSION_SUPPORT, &tmpbool,
		   PMIX_BOOL);
    i++;

    /* Server is acting as a gateway for PMIx requests that cannot be serviced
     * on backend nodes (e.g., logging to email). */
    tmpbool = false;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_SERVER_GATEWAY, &tmpbool, PMIX_BOOL);
    i++;

    /* Server is supporting system scheduler and desires access to appropriate
     * WLM-supporting features. Indicates that the library is to be initialized
     * for scheduler support. */
    tmpbool = false;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_SERVER_SCHEDULER, &tmpbool, PMIX_BOOL);
    i++;

# if 0 /* optional attributes */

    /* Disable legacy UNIX socket (usock) support. */
    tmpbool = false;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_USOCK_DISABLE, &tmpbool, PMIX_BOOL);
    i++;

    /* POSIX mode_t (9 bits valid). */
    uint32_t tmpuint32 = 0;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_SOCKET_MODE, &tmpuint32, PMIX_UINT32);
    i++;

    /* Use only one rendezvous socket, letting priorities and/or environment
     * parameters select the active transport. */
    tmpbool = false;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_SINGLE_LISTENER, &tmpbool, PMIX_BOOL);
    i++;

    /* If provided, directs that the TCP URI be reported and indicates the
     * desired method of reporting: ’-’ for stdout, ’+’ for stderr, or filename.
     * If the library supports TCP socket connections, this attribute may be
     * supported for reporting the URI. */
    tmpstr = "";
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_TCP_REPORT_URI, &tmpstr, PMIX_STRING);
    i++;

    /* Comma-delimited list of devices and/or CIDR notation to include when
     * establishing the TCP connection. If the library supports TCP socket
     * connections, this attribute may be supported for specifying the
     * interfaces to be used. */
    tmpstr = "";
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_TCP_IF_INCLUDE, &tmpstr, PMIX_STRING);
    i++;

    /* Comma-delimited list of devices and/or CIDR notation to exclude when
     * establishing the TCP connection. If the library supports TCP socket
     * connections, this attribute may be supported for specifying the
     * interfaces that are not to be used. */
    tmpstr = "";
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_TCP_IF_EXCLUDE, &tmpstr, PMIX_STRING);
    i++;

    /* The IPv4 port to be used.. If the library supports IPV4 connections, this
     * attribute may be supported for specifying the port to be used. */
    int tmpint = 1234;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_TCP_IPV4_PORT, &tmpint, PMIX_INT);
    i++;

    /* The IPv6 port to be used. If the library supports IPV6 connections, this
     * attribute may be supported for specifying the port to be used. */
    int tmpint = 1234;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_TCP_IPV6_PORT, &tmpint, PMIX_INT);
    i++;

    /* Set to true to disable IPv4 family of addresses. If the library supports
     * IPV4 connections, this attribute may be supported for disabling it. */
    tmpbool = false;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_TCP_DISABLE_IPV4, &tmpbool, PMIX_BOOL);
    i++;

    /* Set to true to disable IPv6 family of addresses. If the library supports
     * IPV6 connections, this attribute may be supported for disabling it. */
    tmpbool = false;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_TCP_DISABLE_IPV6, &tmpbool, PMIX_BOOL);
    i++;

    /* Allow connections from remote tools. Forces the PMIx server to not
     * exclusively use loopback device. */
    tmpbool = false;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_SERVER_REMOTE_CONNECTIONS, &tmpbool,
		   PMIX_BOOL);
    i++;

    /* The host shall progress the PMIx library via calls to PMIx_Progress */
    tmpbool = false;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_EXTERNAL_PROGRESS, &tmpbool,
		   PMIX_BOOL);
    i++;

    /* Pointer to an event_base to use in place of the internal progress thread.
     * All PMIx library events are to be assigned to the provided event base.
     * The event base must be compatible with the event library used by the PMIx
     * implementation - e.g., either both the host and PMIx library must use
     * libevent, or both must use libev. Cross-matches are unlikely to work and
     * should be avoided - it is the responsibility of the host to ensure that
     * the PMIx implementation supports (and was built with) the appropriate
     * event library. */
    void *tmpptr = NULL;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_EVENT_BASE, &tmpptr, PMIX_POINTER);
    i++;

    /* Provide a pointer to an implementation-specific description of the local
     * node topology. */
    pmix_topology_t tmptopo;
    PMIX_TOPOLOGY_CONSTRUCT(&tmptopo);
    tmptopo.source = "custom";
    tmptopo.topology = NULL;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_TOPOLOGY2, &tmptopo, PMIX_TOPO);
    i++;

    /* The PMIx server is to share its copy of the local node topology (whether
     * given to it or self-discovered) with any clients. */
    tmpbool = false;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_SERVER_SHARE_TOPOLOGY, &tmpbool,
		   PMIX_BOOL);
    i++;

    /* Enable PMIx internal monitoring by the PMIx server. */
    tmpbool = false;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_SERVER_ENABLE_MONITORING, &tmpbool,
		   PMIX_BOOL);
    i++;

    /* The nodes comprising the session are homogeneous - i.e., they each
     * contain the same number of identical packages, fabric interfaces, GPUs,
     * and other devices. */
    tmpbool = false;
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_HOMOGENEOUS_SYSTEM, &tmpbool,
		   PMIX_BOOL);
    i++;

    /* Time when the server started - i.e., when the server created it’s
     * rendezvous file (given in ctime string format). */
    time_t tmptime = time(NULL);
    char *tmpstr = ctime(&tmptime);
    PMIX_INFO_LOAD(&cbdata.info[i], PMIX_SERVER_START_TIME, tmpstr,
		   PMIX_STRING);
    i++;
# endif /* optional attributes */

#endif /* if PMIX_VERSION_MAJOR >= 4 */

    mdbg(PSPMIX_LOG_VERBOSE, "%s: Setting nspace %s rank %d\n", __func__,
	    nspace, rank);

#if PMIX_VERSION_MAJOR >= 4
    if (mset(PSPMIX_LOG_INFOARR)) {
	mlog("%s: PMIx_server_init info:\n", __func__);
	for (size_t j = 0; j < cbdata.ninfo; j++) {
	    char * istr = PMIx_Info_string(&cbdata.info[j]);
	    mlog("%s\n", istr);
	    free(istr);
	}
    }
#endif

    /* initialize server library */
    pmix_status_t status = PMIx_server_init(&module, cbdata.info, cbdata.ninfo);
    DESTROY_CBDATA(cbdata);
    if (status != PMIX_SUCCESS) {
	mlog("%s: PMIx_server_init() failed: %s\n", __func__,
	     PMIx_Error_string(status));
	return false;
    }
    mdbg(PSPMIX_LOG_VERBOSE, "%s: PMIx_server_init() successful\n", __func__);

#if PMIX_VERSION_MAJOR >= 4
    /* tell the server common information */

    pmix_data_array_t sessionInfo;
    if (!fillServerSessionArray(&sessionInfo, clusterid)) {
	mlog("%s: filling server session info failed\n", __func__);
	return false;
    }

    INIT_CBDATA(cbdata, 1);
    PMIX_INFO_LOAD(cbdata.info, PMIX_SESSION_INFO_ARRAY, &sessionInfo,
		   PMIX_DATA_ARRAY);

    if (mset(PSPMIX_LOG_INFOARR)) {
	mlog("%s: PMIx_server_register_resources info:\n", __func__);
	for (size_t j = 0; j < cbdata.ninfo; j++)
	    mlog("%s\n", PMIx_Info_string(&cbdata.info[j]));
    }

    status = PMIx_server_register_resources(cbdata.info, cbdata.ninfo,
					    registerResources_cb, &cbdata);
    if (status != PMIX_SUCCESS) {
	mlog("%s: PMIx_server_register_resources() failed: %s\n", __func__,
	     PMIx_Error_string(status));
	DESTROY_CBDATA(cbdata);
	return false;
    }
    WAIT_FOR_CBDATA(cbdata);

    if (cbdata.status != PMIX_SUCCESS) {
	mlog("%s: Callback from register resources failed: %s\n", __func__,
	     PMIx_Error_string(cbdata.status));
	DESTROY_CBDATA(cbdata);
	return false;
    }
    DESTROY_CBDATA(cbdata);

    mdbg(PSPMIX_LOG_VERBOSE, "%s: PMIx_server_register_resources()"
	 " successful\n", __func__);
#endif

    /* register the error handler */
    INIT_CBDATA(cbdata, 0);
    PMIx_Register_event_handler(NULL, 0, NULL, 0,
	    errhandler, registerErrorHandler_cb, &cbdata);
    WAIT_FOR_CBDATA(cbdata);

    if (cbdata.status != PMIX_SUCCESS) {
	mlog("%s: Callback from register error handler failed: %s\n", __func__,
	     PMIx_Error_string(cbdata.status));
	return false;
    }
    DESTROY_CBDATA(cbdata);

    initialized = true;

    return true;
}
#if 0
#define ASPRINTF(...) if (asprintf(__VA_ARGS__) < 0) abort()

static char * getAllocatedInfoString(pmix_info_t *info)
{
    char *type, *value, *ret;
    switch(info->value.type) {
	case PMIX_VAL_TYPE_int:
	    type = "int";
	    ASPRINTF(&value, "%d", PMIX_VAL_FIELD_int(&info->value));
	    break;
	case PMIX_VAL_TYPE_uint32_t:
	    type = "uint32_t";
	    ASPRINTF(&value, "%u", PMIX_VAL_FIELD_uint32_t(&info->value));
	    break;
	case PMIX_VAL_TYPE_uint16_t:
	    type = "uint16_t";
	    ASPRINTF(&value, "%hd", PMIX_VAL_FIELD_uint16_t(&info->value));
	    break;
	case PMIX_VAL_TYPE_string:
	    type = "string";
	    ASPRINTF(&value, "%s", PMIX_VAL_FIELD_string(&info->value));
	    break;
	case PMIX_VAL_TYPE_float:
	    type = "float";
	    ASPRINTF(&value, "%f", PMIX_VAL_FIELD_float(&info->value));
	    break;
	case PMIX_VAL_TYPE_byte:
	    type = "byte";
	    ASPRINTF(&value, "%hhu", PMIX_VAL_FIELD_int(&info->value));
	    break;
	case PMIX_VAL_TYPE_flag:
	    type = "flag";
	    ASPRINTF(&value, "'%s'", (PMIX_INFO_TRUE(info)) ? "TRUE" : "FALSE");
	    break;
	case PMIX_PROC_RANK:
	    type = "rank";
	    ASPRINTF(&value, "%u", info->value.data.rank);
	    break;
	case PMIX_DATA_ARRAY:
	    type = "pmix_data_array_t";
	    if (info->value.data.darray == NULL) {
		ASPRINTF(&value, "%s", "<NULL>");
	    } else {
		if (info->value.data.darray->type == PMIX_INFO) {
		    for (size_t i = 0; i < data.ninfo; i++) {
			char *tmp = getAllocatedInfoString(&data.info[i]);
			mlog("%s: passing %s\n", __func__, tmp);
			free(tmp);
		    }
		    ASPRINTF(&value, "{%s}", info->value.data.darray->size);
		ASPRINTF(&value, "%lu elements", info->value.data.darray->size);
	    }
	    break;
	default:
	    type = "(not supported)";
	    ASPRINTF(&value, "%s", "(unknown)");
    }

    ASPRINTF(&ret, "info[key=%s type=%s value=%s]", info->key, type, value);
    free(value);
    return ret;
}
#endif

#if 0
/**
 * To be called by PMIx_server_setup_application() to provide the informations
 */
static void setupApplication_cb(
	pmix_status_t status,
	pmix_info_t info[], size_t ninfo,
	void *provided_cbdata, pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mycbdata_t *data = provided_cbdata;

    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    data->status = status;

    /* copy provided info into mycbdata */
    if (status == PMIX_SUCCESS && ninfo > 0) {
	data->ninfo = ninfo;
	PMIX_INFO_CREATE(data->info, ninfo);
	for (size_t i = 0; i < ninfo; i++) {
	    PMIX_INFO_XFER(&data->info[i], &info[i]);
	}
    }
    if (cbfunc != NULL) {
	/* tell the server library to free the data */
	cbfunc(PMIX_SUCCESS, cbdata);
    }

    SET_CBDATA_AVAIL(data);
}
#endif

#if 0
/* create comma-delimited string from an integer array */
static char * intArrayToCommaString(uint32_t *array, size_t len)
{
#define CHUNK 512
    size_t retsize = CHUNK;
    char *ret = umalloc(retsize * sizeof(*ret));

    char *ptr = ret;
    size_t rest = retsize;

    for (size_t i = 1; i <= len; i++) {
	size_t s = snprintf(ptr, rest, "%d", array[i-1]);
	if (s >= rest) {
	    retsize += CHUNK;
	    ret = urealloc(ret, retsize * sizeof(*ret));
	    rest += CHUNK;
	    /* try again */
	    i--;
	    continue;
	}
	ptr += s;
	if (i < len) *ptr++ = ',';
	rest -= s + 1;
    }

    return ret;
#undef CHUNK
}
#endif

/*
 * Get string representation of process map
 *
 * (free using ufree())
 */
static char* getProcessMapString(list_t *procMap)
{
    char buf[24];
    char ranksep = ',';
    char nodesep = ';';
    vector_t pmap;
    charvInit(&pmap, 50);

    list_t *n;
    list_for_each(n, procMap) {
	PspmixNode_t *node = list_entry(n, PspmixNode_t, next);
	for (size_t i = 0; i < node->procs.len; i++) {
	    PspmixProcess_t *proc = vectorGet(&node->procs, i, PspmixProcess_t);
	    sprintf(buf, "%u", proc->rank);
	    if (i) charvAdd(&pmap, &ranksep);
	    charvAddCount(&pmap, buf, strlen(buf));
	}
	if (node->next.next != procMap) {
	    charvAdd(&pmap, &nodesep);
	}
    }

    /* terminate string */
    buf[0] = '\0';
    charvAdd(&pmap, buf);

    return (char *)pmap.data;
}

/**
 * Get string representation of nodes ranks
 *
 * @param procMap      process map (which process runs on which node)
 * @param nodeID       parastation node id of this node
 * @param pmap   <OUT> vector to hold the process map string
 * @param lpeers <OUT> vector to hold the local peers string
 *
 * @return true on success, false else
 */
static char* getNodeRanksString(PspmixNode_t *node)
{
    char buf[24];
    char ranksep = ',';
    vector_t ranks;
    charvInit(&ranks, 50);

    for (size_t i = 0; i < node->procs.len; i++) {
	PspmixProcess_t *proc;
	proc = vectorGet(&node->procs, i, PspmixProcess_t);
	sprintf(buf, "%u", proc->rank);
	if (i > 0) charvAdd(&ranks, &ranksep);
	charvAddCount(&ranks, buf, strlen(buf));
    }

    /* terminate string */
    buf[0] = '\0';
    charvAdd(&ranks, buf);

    return ranks.data;
}

static void fillSessionInfoArray(pmix_data_array_t *sessionInfo,
				 uint32_t session_id, uint32_t universe_size)
{
#if PMIX_VERSION_MAJOR >= 4
# define SESSION_INFO_ARRAY_LEN 3
#else
# define SESSION_INFO_ARRAY_LEN 2
#endif
    pmix_info_t *infos;
    PMIX_INFO_CREATE(infos, SESSION_INFO_ARRAY_LEN);

    size_t i = 0;
    /* first entry needs to be session id */
    PMIX_INFO_LOAD(&infos[i], PMIX_SESSION_ID, &session_id, PMIX_UINT32);
    i++;

    /* number of slots in this session */
    PMIX_INFO_LOAD(&infos[i], PMIX_MAX_PROCS, &universe_size, PMIX_UINT32);
    i++;

#if PMIX_VERSION_MAJOR >= 4
    PMIX_INFO_LOAD(&infos[i], PMIX_UNIV_SIZE, &universe_size, PMIX_UINT32);
    i++;
#endif

    /* optional infos (PMIx v4.0):
     * * PMIX_ALLOCATED_NODELIST "pmix.alist" (char*)
     *     Comma-delimited list or regular expression of all nodes in the
     *     specified realm regardless of whether or not they currently host
     *     processes. Defaults to the job realm.
     */

#if PMIX_VERSION_MAJOR < 4
    mdbg(PSPMIX_LOG_INFOARR, "%s: %s(%d)=%u - %s(%d)=%u\n", __func__,
	 infos[0].key, infos[0].value.type, infos[0].value.data.uint32,
	 infos[1].key, infos[1].value.type, infos[1].value.data.uint32);
#endif

    sessionInfo->type = PMIX_INFO;
    sessionInfo->size = SESSION_INFO_ARRAY_LEN;
    sessionInfo->array = infos;
}

static void fillJobInfoArray(pmix_data_array_t *jobInfo, const char *jobId,
			     uint32_t jobSize, uint32_t maxProcs,
			     const char *nodelist_s, list_t *procMap,
			     uint32_t numApps)
{
#define JOB_INFO_ARRAY_LEN 6
    pmix_info_t *infos;
    PMIX_INFO_CREATE(infos, JOB_INFO_ARRAY_LEN);

    size_t i = 0;
    /* job identifier (this is the name of the namespace */
    PMIX_INFO_LOAD(&infos[i], PMIX_JOBID, jobId, PMIX_STRING);
    i++;

    /* total num of processes in this job across all contained applications */
    PMIX_INFO_LOAD(&infos[i], PMIX_JOB_SIZE, &jobSize, PMIX_UINT32);
    i++;

    /* Maximum number of processes in this job */
    PMIX_INFO_LOAD(&infos[i], PMIX_MAX_PROCS, &maxProcs, PMIX_UINT32);
    i++;

    /* regex of nodes containing procs for this job */
    char *nodelist_r;
    PMIx_generate_regex(nodelist_s, &nodelist_r);
    PMIX_INFO_LOAD(&infos[i], PMIX_NODE_MAP, nodelist_r, PMIX_STRING);
    i++;

    /* regex describing procs on each node within this job */
    char *pmap_s;
    pmap_s = getProcessMapString(procMap);

    mdbg(PSPMIX_LOG_INFOARR, "%s: proc_map string created: '%s'\n", __func__,
	 pmap_s);

    char *pmap_r;
    PMIx_generate_ppn(pmap_s, &pmap_r);
    ufree(pmap_s);
    PMIX_INFO_LOAD(&infos[i], PMIX_PROC_MAP, pmap_r, PMIX_STRING);
    i++;

    /* number of applications in this job (required if > 1) */
    PMIX_INFO_LOAD(&infos[i], PMIX_JOB_NUM_APPS, &numApps, PMIX_UINT32);
    i++;

    /* optional infos (PMIx v3.0):
     * * PMIX_SERVER_NSPACE "pmix.srv.nspace" (char*)       (mandatory in v4.0?)
     *     Name of the namespace to use for this PMIx server.
     *
     * * PMIX_SERVER_RANK "pmix.srv.rank" (pmix_rank_t)     (mandatory in v4.0?)
     *     Rank of this PMIx server
     *
     * * PMIX_NPROC_OFFSET "pmix.offset" (pmix_rank_t)
     *     Starting global rank of this job     (XXX: needed for spawn support)
     *
     * * PMIX_ALLOCATED_NODELIST "pmix.alist" (char*)   (v4.0: in session realm)
     *     Comma-delimited list of all nodes in this allocation regardless of
     *     whether or not they currently host processes
     *
     * * PMIX_MAPBY "pmix.mapby" (char*)
     *     Process mapping policy
     *
     * * PMIX_RANKBY "pmix.rankby" (char*)
     *     Process ranking policy
     *
     * * PMIX_BINDTO "pmix.bindto" (char*)
     *     Process binding policy
     *
     * optional infos (PMIx v4.0):
     * * PMIX_HOSTNAME_KEEP_FQDN "pmix.fqdn" (bool)
     *     FQDNs are being retained by the PMIx library.
     *
     * * PMIX_TDIR_RMCLEAN "pmix.tdir.rmclean" (bool)
     *     Resource Manager will cleanup assigned temporary directory trees.
     *
     * * PMIX_CRYPTO_KEY "pmix.sec.key" (pmix_byte_object_t)
     *     Blob containing crypto key.
     */

#if PMIX_VERSION_MAJOR < 4
    mdbg(PSPMIX_LOG_INFOARR, "%s: %s(%d)='%s' - %s(%d)=%u - %s(%d)=%u - "
	 "%s(%d)='%s' - %s(%d)='%s' - %s(%d)=%u\n", __func__,
	 infos[0].key, infos[0].value.type, infos[0].value.data.string,
	 infos[1].key, infos[1].value.type, infos[1].value.data.uint32,
	 infos[2].key, infos[2].value.type, infos[2].value.data.uint32,
	 infos[3].key, infos[3].value.type, infos[3].value.data.string,
	 infos[4].key, infos[4].value.type, infos[4].value.data.string,
	 infos[5].key, infos[5].value.type, infos[5].value.data.uint32);
#endif

    jobInfo->type = PMIX_INFO;
    jobInfo->size = JOB_INFO_ARRAY_LEN;
    jobInfo->array = infos;
}

static void fillAppInfoArray(pmix_data_array_t *appInfo, PspmixApp_t *app)
{
#if PMIX_VERSION_MAJOR < 4
#define APP_INFO_ARRAY_LEN 4
#else
#define APP_INFO_ARRAY_LEN 5
#endif
    pmix_info_t *infos;
    PMIX_INFO_CREATE(infos, APP_INFO_ARRAY_LEN);

    size_t i = 0;
    /* application number */
    PMIX_INFO_LOAD(&infos[i], PMIX_APPNUM, &app->num, PMIX_UINT32);
    i++;

    /* number of processes in this application */
    PMIX_INFO_LOAD(&infos[i], PMIX_APP_SIZE, &app->size, PMIX_UINT32);
    i++;

    /* lowest rank in this application within the job */
    PMIX_INFO_LOAD(&infos[i], PMIX_APPLDR, &app->firstRank, PMIX_PROC_RANK);
    i++;

    /* working directory for spawned processes */
    PMIX_INFO_LOAD(&infos[i], PMIX_WDIR, app->wdir, PMIX_STRING);
    i++;

#if PMIX_VERSION_MAJOR >= 4
    /* concatenated argv for spawned processes */
    PMIX_INFO_LOAD(&infos[i], PMIX_APP_ARGV, app->args, PMIX_STRING);
    i++;
#endif

    /* optional infos (PMIx v4.0):
     * * PMIX_PSET_NAMES "pmix.pset.nms" (pmix_data_array_t*)
     *     Returns an array of char* string names of the process sets in which
     *     the given process is a member.
     *
     * * PMIX_APP_MAP_TYPE "pmix.apmap.type" (char*)
     *     Type of mapping used to layout the application (e.g., cyclic).
     *
     * * PMIX_APP_MAP_REGEX "pmix.apmap.regex" (char*)
     *     Regular expression describing the result of the process mapping.
     *
     * * PMIX_PROGRAMMING_MODEL "pmix.pgm.model" (char*)
     *     Programming model being initialized (e.g., “MPI” or “OpenMP”).
     *
     * * PMIX_MODEL_LIBRARY_NAME "pmix.mdl.name" (char*)
     *     Programming model implementation ID (e.g., “OpenMPI” or “MPICH”).
     *
     * * PMIX_MODEL_LIBRARY_VERSION "pmix.mld.vrs" (char*)
     *     Programming model version string (e.g., “2.1.1”).
     */

#if PMIX_VERSION_MAJOR < 4
    mdbg(PSPMIX_LOG_INFOARR, "%s: %s(%d)=%u - %s(%d)=%u - %s(%d)=%u - "
	 "%s(%d)='%s'\n", __func__,
	 infos[0].key, infos[0].value.type, infos[0].value.data.uint32,
	 infos[1].key, infos[1].value.type, infos[1].value.data.uint32,
	 infos[2].key, infos[2].value.type, infos[2].value.data.rank,
	 infos[3].key, infos[3].value.type, infos[3].value.data.string);
#endif

    appInfo->type = PMIX_INFO;
    appInfo->size = APP_INFO_ARRAY_LEN;
    appInfo->array = infos;
}

#if PMIX_VERSION_MAJOR >= 4
/**
 * @param nodeInfo     array to fill
 * @param node         node object
 * @param id           id of the node in the session (PMIX_NODEID)
 * @param tmpdir       temporary directory of the session
 * @param nsdir        temporary directory of the namespace relative to tmpdir
 */
static void fillNodeInfoArray(pmix_data_array_t *nodeInfo, PspmixNode_t *node,
			      uint32_t id, const char *tmpdir,
			      const char *nsdir)
{
    uint32_t ninfo = 5;
    if (node->id == PSC_getMyID()) ninfo += 2;

    pmix_info_t *infos;
    PMIX_INFO_CREATE(infos, ninfo);

    size_t i = 0;
    /* node id (in the session) */
    PMIX_INFO_LOAD(&infos[i], PMIX_NODEID, &id, PMIX_UINT32);
    i++;

    /* hostname */
    PMIX_INFO_LOAD(&infos[i], PMIX_HOSTNAME, node->hostname, PMIX_STRING);
    i++;

    /* number of processes on the node (in this namespace) */
    PMIX_INFO_LOAD(&infos[i], PMIX_LOCAL_SIZE, &node->procs.len, PMIX_UINT32);
    i++;

    /* Note: PMIX_NODE_SIZE (processes over all the user's jobs)
     * managed by pmix_register_resources @todo
     * https://github.com/pmix/pmix-standard/issues/401*/

    /* lowest rank on this node within this job/namespace */
    PspmixProcess_t *proc = vectorGet(&node->procs, 0, PspmixProcess_t);
    PMIX_INFO_LOAD(&infos[i], PMIX_LOCALLDR, &proc->rank, PMIX_PROC_RANK);
    i++;

    /* Comma-delimited list of ranks that are executing on the node
     * within this namespace */
    char *lpeers;
    lpeers = getNodeRanksString(node);
    if (lpeers[0] == '\0') mlog("%s: no local ranks for node %u (%s)\n",
				__func__, id, node->hostname);
    PMIX_INFO_LOAD(&infos[i], PMIX_LOCAL_PEERS, lpeers, PMIX_STRING);
    i++;
    ufree(lpeers);

    /* optional infos (PMIx v4.0):
     * * PMIX_MAX_PROCS "pmix.max.size" (uint32_t)
     *     Maximum number of processes that can be executed in the specified
     *     realm. Typically, this is a constraint imposed by a scheduler or by
     *     user settings in a hostfile or other resource description. Defaults
     *     to the job realm.
     */

    if (node->id == PSC_getMyID()) {

	/* Full path to the top-level temporary directory assigned to the
	 * session */
	PMIX_INFO_LOAD(&infos[i], PMIX_TMPDIR, tmpdir, PMIX_STRING);
	i++;

	/* Full path to the temporary directory assigned to the specified job,
	 * under PMIX_TMPDIR. */
	PMIX_INFO_LOAD(&infos[i], PMIX_NSDIR, nsdir, PMIX_STRING);
	i++;

	/* Array of pmix_proc_t of all processes executing on the local node */
	//@todo how to implement that, standard ambiguous?
	//PMIX_LOCAL_PROCS "pmix.lprocs" (pmix_proc_t array)

	/* optional infos (PMIx v4.0):
	 * * PMIX_LOCAL_CPUSETS "pmix.lcpus" (pmix_data_array_t)
	 *     (this was required in PMIx v3.0)
	 *     A pmix_data_array_t array of string representations of the PU
	 *     binding bitmaps applied to each local peer on the caller’s node
	 *     upon launch. Each string shall begin with the name of the library
	 *     that generated it (e.g., "hwloc") followed by a colon and the
	 *     bitmap string itself. The array shall be in the same order as the
	 *     processes returned by PMIX_LOCAL_PEERS for that namespace.
	 *
	 * * PMIX_AVAIL_PHYS_MEMORY "pmix.pmem" (uint64_t)
	 *     Total available physical memory on a node.
	 *     As this information is not related to the namespace, it can be
	 *     passed using the PMIx_server_register_resources API.
	 */
    }

#if PMIX_VERSION_MAJOR < 4
    mdbg(PSPMIX_LOG_INFOARR, "%s: %s(%d)=%u - %s(%d)='%s' - %s(%d)=%u - "
	 "%s(%d)=%u - %s(%d)='%s'", __func__,
	 infos[0].key, infos[0].value.type, infos[0].value.data.uint32,
	 infos[1].key, infos[1].value.type, infos[1].value.data.string,
	 infos[2].key, infos[2].value.type, infos[2].value.data.uint32,
	 infos[3].key, infos[3].value.type, infos[3].value.data.rank,
	 infos[4].key, infos[4].value.type, infos[4].value.data.string);
    if (node->id == PSC_getMyID()) {
	mdbg(PSPMIX_LOG_INFOARR, " - %s(%d)='%s' - %s(%d)='%s'",
	 infos[5].key, infos[5].value.type, infos[5].value.data.string,
	 infos[6].key, infos[6].value.type, infos[6].value.data.string);
    }
    mdbg(PSPMIX_LOG_INFOARR, "\n");
#endif

    nodeInfo->type = PMIX_INFO;
    nodeInfo->size = ninfo;
    nodeInfo->array = infos;
}
#endif /* PMIX_VERSION_MAJOR >= 4 */

static void fillProcDataArray(pmix_data_array_t *procData,
			      PspmixProcess_t *proc, PSnodes_ID_t nodeID,
			      bool spawned, const char *nsdir)
{
#if PMIX_VERSION_MAJOR < 4
    uint32_t ninfo = 8;
#else
    uint32_t ninfo = 9;
    if (nodeID == PSC_getMyID()) ninfo += 3;
#endif
    pmix_info_t *infos;
    PMIX_INFO_CREATE(infos, ninfo);

    size_t i = 0;
    /* process rank within the job, starting from zero */
    PMIX_INFO_LOAD(&infos[i], PMIX_RANK, &proc->rank, PMIX_PROC_RANK);
    i++;

    /* application number within the job in which the process is a member. */
    PMIX_INFO_LOAD(&infos[i], PMIX_APPNUM, &proc->app->num, PMIX_UINT32);
    i++;

    /* rank within the process' application */
    PMIX_INFO_LOAD(&infos[i], PMIX_APP_RANK, &proc->arank, PMIX_PROC_RANK);
    i++;

    /* rank of the process spanning across all jobs in this session
     * starting with zero.
     * Note that no ordering of the jobs is implied when computing this value.
     * As jobs can start and end at random times, this is defined as a
     * continually growing number - i.e., it is not dynamically adjusted as
     * individual jobs and processes are started or terminated. */
    PMIX_INFO_LOAD(&infos[i], PMIX_GLOBAL_RANK, &proc->grank, PMIX_PROC_RANK);
    i++;

    /* rank of the process on its node in its job
     * refers to the numerical location (starting from zero) of the process on
     * its node when idxing only those processes from the same job that share
     * the node, ordered by their overall rank within that job. */
    PMIX_INFO_LOAD(&infos[i], PMIX_LOCAL_RANK, &proc->lrank, PMIX_UINT16);
    i++;

    /* rank of the process on its node spanning all jobs
     * refers to the numerical location (starting from zero) of the process on
     * its node when idxing all processes (regardless of job) that share the
     * node, ordered by their overall rank within the job. The value represents
     * a snapshot in time when the specified process was started on its node and
     * is not dynamically adjusted as processes from other jobs are started or
     * terminated on the node. */
    PMIX_INFO_LOAD(&infos[i], PMIX_NODE_RANK, &proc->nrank, PMIX_UINT16);
    i++;

    /* node identifier where the process is located */
    uint32_t val_u32 = nodeID;
    PMIX_INFO_LOAD(&infos[i], PMIX_NODEID, &val_u32, PMIX_UINT32);
    i++;

    /* true if this proc resulted from a call to PMIx_Spawn */
    PMIX_INFO_LOAD(&infos[i], PMIX_SPAWNED, &spawned, PMIX_BOOL);
    i++;

#if PMIX_VERSION_MAJOR >= 4
    /* number of times this process has been re-instantiated
     * i.e, a value of zero indicates that the process has never been restarted.
     */
    PMIX_INFO_LOAD(&infos[i], PMIX_REINCARNATION, &proc->reinc, PMIX_UINT32);
    i++;

    /* optional infos (PMIx v4.0):
     *
     * * PMIX_HOSTNAME "pmix.hname" (char*)
     *     Name of the host where the specified process is running.
     *
     * * PMIX_PROCID "pmix.procid" (pmix_proc_t)
     *     Process identifier
     *
     * * PMIX_CPUSET "pmix.cpuset" (char*)
     *     A string representation of the PU binding bitmap applied to the
     *     process upon launch. The string shall begin with the name of the
     *     library that generated it (e.g., "hwloc") followed by a colon and
     *     the bitmap string itself.
     *
     * * PMIX_CPUSET_BITMAP "pmix.bitmap" (pmix_cpuset_t*)
     *     Bitmap applied to the process upon launch.
     *
     * * PMIX_DEVICE_DISTANCES "pmix.dev.dist" (pmix_data_array_t)
     *     Return an array of pmix_device_distance_t containing the minimum and
     *     maximum distances of the given process location to all devices of the
     *     specified type on the local node.
     */

    if (nodeID == PSC_getMyID()) {
	/* string describing a process’s bound location
	 * referenced using the process’s rank. The string is prefixed by the
	 * implementation that created it (e.g., "hwloc") followed by a colon.
	 * The remainder of the string represents the corresponding locality as
	 * expressed by the underlying implementation. The entire string must be
	 * passed to PMIx_Get_relative_locality for processing. Note that hosts
	 * are only required to provide locality strings for local client
	 * processes - thus, a call to PMIx_Get for the locality string of a
	 * process that returns PMIX_ERR_NOT_FOUND indicates that the process is
	 * not executing on the same node. */
	char *locstr;
	pmix_cpuset_t cpuset;
	PMIX_CPUSET_CONSTRUCT(&cpuset); /* @todo deprecated in 4.2.3 */
	cpuset.source = "hwloc";
	cpuset.bitmap = hwloc_bitmap_alloc();
	for (int cpu = 0; cpu < PSIDnodes_getNumThrds(nodeID); cpu++) {
	    if (!PSCPU_isSet(proc->cpus, cpu)) continue;
	    hwloc_bitmap_set(cpuset.bitmap,
			     PSIDnodes_unmapCPU(PSC_getMyID(), cpu));
	}
	pmix_status_t status = PMIx_server_generate_locality_string(&cpuset,
								    &locstr);
	hwloc_bitmap_free(cpuset.bitmap);
	cpuset.source = NULL; /* prevent free of const by destruct func */
	cpuset.bitmap = NULL; /* prevent double free by destruct func */
	PMIx_Cpuset_destruct(&cpuset);

	if (status != PMIX_SUCCESS) {
	    mlog("%s: failed to generate locality string for rank %d: %s\n",
		 __func__, proc->rank, PMIx_Error_string(status));
	    locstr = strdup("pspmix:generation_error");
	    if (!locstr) abort(); /* @todo handle somehow more gently? */
	}
	PMIX_INFO_LOAD(&infos[i], PMIX_LOCALITY_STRING, locstr, PMIX_STRING);
	i++;
	free(locstr);

	/* Full path to the subdirectory under PMIX_NSDIR assigned to the
	 * specified process. */
	int pdsize = strlen(nsdir)+10;
	char *procdir = umalloc(pdsize);
	if (snprintf(procdir, pdsize, "%s/%u", nsdir, proc->rank) >= pdsize) {
	    mlog("%s: Warning, procdir truncated", __func__);
	}
	PMIX_INFO_LOAD(&infos[i], PMIX_PROCDIR, procdir, PMIX_STRING);
	i++;
	ufree(procdir);

	/* rank of the process on the package (socket) where this process
	 * resides refers to the numerical location (starting from zero) of the
	 * process on its package when counting only those processes from the
	 * same job that share the package, ordered by their overall rank within
	 * that job. Note that processes that are not bound to PUs within a
	 * single specific package cannot have a package rank. */
	uint16_t pkgrank = 0; /* @todo how to get this here? */
	PMIX_INFO_LOAD(&infos[i], PMIX_PACKAGE_RANK, &pkgrank, PMIX_UINT16);
	i++;
    }
#endif

#if PMIX_VERSION_MAJOR < 4
    mdbg(PSPMIX_LOG_INFOARR, "%s: %s(%d)=%u - %s(%d)=%u - %s(%d)=%u - "
	 "%s(%d)=%u - %s(%d)=%hu - %s(%d)=%hu - %s(%d)=%u - %s(%d)=%d\n",
	 __func__,
	 infos[0].key, infos[0].value.type, infos[0].value.data.rank,
	 infos[1].key, infos[1].value.type, infos[1].value.data.uint32,
	 infos[2].key, infos[2].value.type, infos[2].value.data.rank,
	 infos[3].key, infos[3].value.type, infos[3].value.data.rank,
	 infos[4].key, infos[4].value.type, infos[4].value.data.uint16,
	 infos[5].key, infos[5].value.type, infos[5].value.data.uint16,
	 infos[6].key, infos[6].value.type, infos[6].value.data.uint32,
	 infos[7].key, infos[7].value.type, infos[7].value.data.flag);
#endif

    procData->type = PMIX_INFO;
    procData->size = ninfo;
    procData->array = infos;
}

/**
 * To be called by PMIx_register_namespace() to provide status
 */
static void registerNamespace_cb(pmix_status_t status, void *cbdata)
{
    mycbdata_t *data = cbdata;

    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    data->status = status;

    SET_CBDATA_AVAIL(data);
}

bool pspmix_server_registerNamespace(const char *nspace, uint32_t sessionId,
				     uint32_t univSize, uint32_t jobSize,
				     bool spawned, uint32_t numNodes,
				     const char *nodelist_s, list_t *procMap,
				     uint32_t numApps, PspmixApp_t *apps,
				     const char *tmpdir, const char *nsdir,
				     PSnodes_ID_t nodeID)
{
    mdbg(PSPMIX_LOG_CALL, "%s(nspace '%s' sessionId %u univSize %u jobSize %u"
	 " spawned %d numNodes %d nodelist_s '%s' numApps %u tmpdir '%s' nsdir '%s'"
	 " nodeID %hd)\n", __func__, nspace, sessionId, univSize, jobSize,
	 spawned, numNodes, nodelist_s, numApps, tmpdir, nsdir, nodeID);

    pmix_status_t status;

    if (univSize < 1) {
	mlog("%s: Bad parameter: univSize = %u (< 1)\n", __func__, univSize);
	return false;
    }
    if (jobSize < 1) {
	mlog("%s: Bad parameter: jobSize = %u (< 1)\n", __func__, jobSize);
	return false;
    }

#if 0

    !!! see https://github.com/pmix/pmix-standard/issues/157 !!!

    /* request application setup information - e.g., network
     * security keys or endpoint info */
    mycbdata_t cbdata;
    INIT_CBDATA(cbdata);
    status = PMIx_server_setup_application(nspace, NULL, 0, setupApplication_cb,
	    &cbdata);
    if (status != PMIX_SUCCESS) {
	mlog("%s: Failed to setup application: %s\n", __func__,
	     PMIx_Error_string(status));
	return false;
    }
    /* wait until the callback function has filled cbdata */
    WAIT_FOR_CBDATA(cbdata);

    if (cbdata.status != PMIX_SUCCESS) {
	mlog("%s: Callback from setup application failed: %s\n", __func__,
	     PMIx_Error_string(cbdata.status));
	return false;
    }

    mdbg(PSPMIX_LOG_VERBOSE, "%s: Got %lu info entries from"
	    " PMIx_server_setup_application().\n", __func__, cbdata.ninfo);

    /* TODO save or return the received data? */
#endif

    /* find this node in procMap */
    PspmixNode_t *mynode = findNodeInList(nodeID, procMap);
    if (mynode == NULL) {
	mlog("%s: Could not find my own node (%u) in process map\n", __func__,
	     nodeID);
	return false;
    }

    /* fill infos */
    mycbdata_t data;
#if PMIX_VERSION_MAJOR >= 4
    INIT_CBDATA(data, 2 + numApps + numNodes + jobSize);
#else
    INIT_CBDATA(data, 4 + numApps + jobSize + 2);
#endif

    size_t i = 0;
#if PMIX_VERSION_MAJOR < 4
    /* number of allocated slots in a session (here for historical reasons) */
    PMIX_INFO_LOAD(&data.info[i], PMIX_UNIV_SIZE, &univSize, PMIX_UINT32);
    i++;
#endif

    /* ===== session info array ===== */
    pmix_data_array_t sessionInfo;
    fillSessionInfoArray(&sessionInfo, sessionId, univSize);
    PMIX_INFO_LOAD(&data.info[i], PMIX_SESSION_INFO_ARRAY, &sessionInfo,
		   PMIX_DATA_ARRAY);
    i++;

#if PMIX_VERSION_MAJOR < 4
    /* total num of processes in this job (here for historical reasons) */
    PMIX_INFO_LOAD(&data.info[i], PMIX_JOB_SIZE, &jobSize, PMIX_UINT32);
    i++;
#endif

    /* ===== job info array ===== */
    pmix_data_array_t jobInfo;
    fillJobInfoArray(&jobInfo, nspace, jobSize, univSize, nodelist_s,
		     procMap, numApps);
    PMIX_INFO_LOAD(&data.info[i], PMIX_JOB_INFO_ARRAY, &jobInfo,
		   PMIX_DATA_ARRAY);
    i++;

    /* ===== application info arrays ===== */
    for (uint32_t j = 0; j < numApps; j++) {
	pmix_data_array_t appInfo;
	fillAppInfoArray(&appInfo, &apps[j]);

	PMIX_INFO_LOAD(&data.info[i], PMIX_APP_INFO_ARRAY, &appInfo,
		       PMIX_DATA_ARRAY);
	i++;
    }

    list_t *n;

#if PMIX_VERSION_MAJOR >= 4
    /* ===== node info arrays ===== */
    uint32_t nodeIdx = 0;
    list_for_each(n, procMap) {
	PspmixNode_t *node = list_entry(n, PspmixNode_t, next);
	pmix_data_array_t nodeInfo;
	fillNodeInfoArray(&nodeInfo, node, nodeIdx++, tmpdir, nsdir);

	PMIX_INFO_LOAD(&data.info[i], PMIX_NODE_INFO_ARRAY, &nodeInfo,
		       PMIX_DATA_ARRAY);
	i++;
    }
#endif

    /* ===== process data ===== */

    /* information about all global ranks */
    list_for_each(n, procMap) {
	PspmixNode_t *node = list_entry(n, PspmixNode_t, next);
	for (size_t j = 0; j < node->procs.len; j++) {
	    PspmixProcess_t *proc = vectorGet(&node->procs, j, PspmixProcess_t);
	    pmix_data_array_t procData;
	    fillProcDataArray(&procData, proc, node->id, spawned, nsdir);

	    PMIX_INFO_LOAD(&data.info[i], PMIX_PROC_DATA, &procData,
			   PMIX_DATA_ARRAY);
	    i++;
	}
    }

#if PMIX_VERSION_MAJOR < 4
    /* ===== own node info ===== */

    /* number of processes in this job/namespace on this node */
    uint32_t val_u32 = mynode->procs.len;
    PMIX_INFO_LOAD(&data.info[i], PMIX_LOCAL_SIZE, &val_u32, PMIX_UINT32);
    i++;

    /* comma-delimited string of ranks on this node within the specified job */
    char *lpeers;
    lpeers = getNodeRanksString(mynode);
    if (lpeers[0] == '\0') {
	mlog("%s: no local ranks found.\n", __func__);
	ufree(lpeers);
	DESTROY_CBDATA(data);
	return false;
    }
    PMIX_INFO_LOAD(&data.info[i], PMIX_LOCAL_PEERS, lpeers, PMIX_STRING);
    i++;
    ufree(lpeers);
#endif

    if (i != data.ninfo) {
	mlog("%s: WARNING: Number of info fields does not match (%lu != %lu)\n",
	     __func__, i, data.ninfo);
    }

    /* debugging output of info values */
    if (mset(PSPMIX_LOG_INFOARR)) {
#if PMIX_VERSION_MAJOR >= 4
	mlog("%s: PMIx_server_register_nspace info:\n", __func__);
	for (size_t j = 0; j < data.ninfo; j++) {
	    char * istr = PMIx_Info_string(&data.info[j]);
	    mlog("%s\n", istr);
	    free(istr);
	}
#else
	vector_t infostr;
	charvInit(&infostr, 1024);

	char prefix[50];
	snprintf(prefix, 50, "%s:   ", __func__);

	for (size_t j = 0; j < data.ninfo; j++) {
	    char *tmpstr;
	    switch(PMIx_Data_print(&tmpstr, prefix, &data.info[j], PMIX_INFO)) {
		case PMIX_SUCCESS:
		    charvAddCount(&infostr, tmpstr, strlen(tmpstr));
		    charvAdd(&infostr, "\n");
		    free(tmpstr);
		    continue;
		case PMIX_ERR_BAD_PARAM:
		    mlog("%s: Data type not recognized by PMIx_Data_print()\n",
			 __func__);
		    break;
		case PMIX_ERR_NOT_SUPPORTED:
		    mlog("%s: PMIx_Data_print() not supported by PMIx"
			 " implementation.\n", __func__);
		    break;
		default:
		    break;
	    }
	    break;
	}
	mdbg(PSPMIX_LOG_INFOARR, "%s: info array to be passed to"
	     " PMIx_server_register_nspace():\n%s", __func__,
	     (char *)infostr.data);
	charvDestroy(&infostr);
#endif
    }

    /* register namespace */
    status = PMIx_server_register_nspace(nspace, mynode->procs.len, data.info,
	    data.ninfo, registerNamespace_cb, &data);
    if (status == PMIX_OPERATION_SUCCEEDED) {
	goto reg_nspace_success;
    }

    if (status != PMIX_SUCCESS) {
	mlog("%s: PMIx_server_register_nspace() failed.\n", __func__);
	goto reg_nspace_error;
    }
    WAIT_FOR_CBDATA(data);

    if (data.status != PMIX_SUCCESS) {
	mlog("%s: Callback from register namespace failed: %s\n", __func__,
		PMIx_Error_string(data.status));
	goto reg_nspace_error;
    }

reg_nspace_success:
    DESTROY_CBDATA(data);
    return true;

reg_nspace_error:
    DESTROY_CBDATA(data);
    return false;
}

/**
 * To be called by PMIx_deregister_namespace() to provide status
 */
static void deregisterNamespace_cb(pmix_status_t status, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    const char *errstr = "";
    if (status != PMIX_SUCCESS) errstr = PMIx_Error_string(status);

    pspmix_service_cleanupNamespace(cbdata, (status != PMIX_SUCCESS), errstr);
}

void pspmix_server_deregisterNamespace(const char *nsname, void *nsobject)
{
    mdbg(PSPMIX_LOG_CALL, "%s(namespace '%s')\n", __func__, nsname);

    /* deregister namespace */
    PMIx_server_deregister_nspace(nsname, deregisterNamespace_cb, nsobject);
}

/**
 * Callback for a call of PMIx_server_setup_local_support()
 */
static void setupLocalSupport_cb(pmix_status_t status, void *cbdata)
{
    mycbdata_t *data = cbdata;

    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    data->status = status;

    SET_CBDATA_AVAIL(data);
}

/* run this function once per node */
bool pspmix_server_setupLocalSupport(const char *nspace)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    pmix_status_t status;

    /* prep the local node for launch
     * TODO pass the info got from PMIx_server_setup_application()
     * needed for PMIx fabric plugin support
     * see https://github.com/pmix/pmix-standard/issues/157 */
    mycbdata_t cbdata;
    INIT_CBDATA(cbdata, 0);
    status = PMIx_server_setup_local_support(nspace, NULL, 0,
	    setupLocalSupport_cb, &cbdata);
    if (status != PMIX_SUCCESS) {
	mlog("%s: Setting up the local support callback failed: %s\n",
	     __func__, PMIx_Error_string(status));
	return false;
    }
    WAIT_FOR_CBDATA(cbdata);

    if (cbdata.status != PMIX_SUCCESS) {
	mlog("%s: Callback from setup local support failed: %s\n", __func__,
	     PMIx_Error_string(cbdata.status));
	return false;
    }
    DESTROY_CBDATA(cbdata);

    return true;
}

/**
 * Callback for a call of PMIx_server_register_client()
 */
static void registerClient_cb(pmix_status_t status, void *cbdata)
{
    mycbdata_t *data = cbdata;

    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    data->status = status;

    SET_CBDATA_AVAIL(data);
}

/* run this function at the server once per client */
bool pspmix_server_registerClient(const char *nspace, int rank, int uid,
				  int gid, void *clientObject)
{
    mdbg(PSPMIX_LOG_CALL, "%s(nspace '%s' rank %d uid %d gid %d)\n", __func__,
	 nspace, rank, uid, gid);

    pmix_status_t status;

    /* setup process struct */
    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    PMIX_PROC_LOAD(&proc, nspace, rank);

    /* register clients uid and gid as well as ident object */
    mycbdata_t cbdata;
    INIT_CBDATA(cbdata, 0);
    status = PMIx_server_register_client(&proc, uid, gid, clientObject,
					 registerClient_cb, &cbdata);
    PMIX_PROC_DESTRUCT(&proc);
    if (status != PMIX_SUCCESS) {
	mlog("%s: Registering client failed: %s\n", __func__,
	     PMIx_Error_string(status));
	return false;
    }
    WAIT_FOR_CBDATA(cbdata);

    if (cbdata.status != PMIX_SUCCESS) {
	mlog("%s: Callback from register client failed: %s\n", __func__,
	     PMIx_Error_string(cbdata.status));
	return false;
    }
    DESTROY_CBDATA(cbdata);

    return true;
}

#if PMIX_VERSION_MAJOR < 4
/**
 * To be called by PMIx_deregisterClient() to provide status
 */
static void deregisterClient_cb(pmix_status_t status, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mycbdata_t *data = cbdata;
    data->status = status;

    SET_CBDATA_AVAIL(data);
}

#endif
/* run this function in exception case to remove all client information  */
void pspmix_server_deregisterClient(const char *nspace, int rank)
{
    mdbg(PSPMIX_LOG_CALL, "%s(nspace '%s' rank %d)\n", __func__, nspace, rank);

    /* setup process struct */
    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    PMIX_PROC_LOAD(&proc, nspace, rank);

    /* deregister client */
#if PMIX_VERSION_MAJOR >= 4
    PMIx_server_deregister_client(&proc, NULL, NULL);
#else
    mycbdata_t cbdata;
    INIT_CBDATA(cbdata, 0);
    PMIx_server_deregister_client(&proc, deregisterClient_cb, &cbdata);
    WAIT_FOR_CBDATA(cbdata);

    if (cbdata.status != PMIX_SUCCESS) {
	mlog("%s: Callback from register client failed: %s\n", __func__,
	     PMIx_Error_string(cbdata.status));
    }

    DESTROY_CBDATA(cbdata);
#endif
    PMIX_PROC_DESTRUCT(&proc);
}

/* get the client environment set */
bool pspmix_server_setupFork(const char *nspace, int rank, char ***childEnv)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    pmix_status_t status;

    /* setup process struct */
    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    PMIX_PROC_LOAD(&proc, nspace, rank);

    /* setup environment */
    status = PMIx_server_setup_fork(&proc, childEnv);
    PMIX_PROC_DESTRUCT(&proc);
    if (status != PMIX_SUCCESS) {
	mlog("%s: Setting up environment for fork failed: %s\n",
	     __func__, PMIx_Error_string(status));
	return false;
    }

    return true;
}

bool pspmix_server_finalize(void)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    pmix_status_t status;

    if (!initialized) {
	mlog("%s: pspmix server not initialized\n", __func__);
	return false;
    }

    /* deregister the errhandler */
    PMIx_Deregister_event_handler(0, NULL, NULL);

    status = PMIx_server_finalize();
    if (status != PMIX_SUCCESS) {
	mlog("%s: PMIx_server_finalize() failed: %s\n", __func__,
	     PMIx_Error_string(status));
	return false;
    }
    return true;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
