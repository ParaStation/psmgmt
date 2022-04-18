/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
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
#include <unistd.h>

#include <pmix_server.h>

#include "list.h"
#include "pluginmalloc.h"
#include "pluginvector.h"

#include "pspmixlog.h"
#include "pspmixservice.h"

/* allow walking throu the environment */
extern char **environ;

/* Set to 1 to enable output of the namespace info fields */
#define PRINT_FILLINFOS 0

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
#define INIT_CBDATA(d) memset(&(d), 0, sizeof(d))

/** Waiting for data to be filled by callback function */
#define WAIT_FOR_CBDATA(d) while(!(d).filled) usleep(10)

/** Set data to be available (used in callback function) */
#define SET_CBDATA_AVAIL(d) (d)->filled = true

/** Setting up data for callback routines */
#define DESTROY_CBDATA(d) PMIX_INFO_FREE((d).info, (d).ninfo)

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

/* Notify the host server that a client connected to us - note
 * that the client will be in a blocked state until the host server
 * executes the callback function, thus allowing the PMIx server support
 * library to release the client */
/* pmix_server_client_connected_fn_t */
static pmix_status_t server_client_connected_cb(const pmix_proc_t *proc,
	void *clientObject, pmix_op_cbfunc_t cbfunc, void *cbdata)
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

/* Notify the host server that a client called PMIx_Finalize - note
 * that the client will be in a blocked state until the host server
 * executes the callback function, thus allowing the PMIx server support
 * library to release the client */
/* pmix_server_client_finalized_fn_t */
static pmix_status_t server_client_finalized_cb(const pmix_proc_t *proc,
	void* clientObject, pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL,
	 "%s(proc(%d, '%s') clientObject %p cbfunc %p cbdata %p)\n", __func__,
	 proc->rank, proc->nspace, clientObject, cbfunc, cbdata);

    mlog("Finalization of %s:%d notified\n", proc->nspace, proc->rank);

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

/* A local client called PMIx_Abort - note that the client will be in a blocked
 * state until the host server executes the callback function, thus
 * allowing the PMIx server support library to release the client. The
 * array of procs indicates which processes are to be terminated. A NULL
 * indicates that all procs in the client's nspace are to be terminated */
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
    }
    else {
	for (size_t i = 0; i < nprocs; i++) {
	    mlog(" %s%s:%d", i ? "," : "", proc[i].nspace, proc[i].rank);
	}
	mlog(" (not supported)\n");

	// we do currently not support aborting subsets of namespaces
	return PMIX_ERR_NOT_SUPPORTED;
    }

    pspmix_service_abort(clientObject);

    return PMIX_OPERATION_SUCCEEDED;
}

/* free everything to be freed in fence stuff */
static void fencenb_release_fn(void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    modexdata_t *mdata = cbdata;

    if (mdata->data) {
	ufree(mdata->data);
	memset(mdata, 0, sizeof(*mdata)); /* only for debugging */
    }

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

    mdbg(PSPMIX_LOG_CALL, "%s(success %s, mdata->cbfunc %s, mdata->ndata %lu\n",
	 __func__, success ? "true" : "false", mdata->cbfunc ? "set" : "unset",
	 mdata->ndata);

    pmix_status_t status = success ? PMIX_SUCCESS : PMIX_ERROR;

    /* Return modex data. The returned blob contains the data collected from
     * each server participating in the operation.
     * As the data is "owned" by the host server, provide a secondary callback
     * function to notify the host server that we are done with the data so it
     * can be released */
    if (mdata->cbfunc) {
	mdata->cbfunc(status, mdata->data, mdata->ndata, mdata->cbdata,
		fencenb_release_fn, mdata);
    }
    else {
	assert(mdata->data == NULL);
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
	    }
	    else {
		snprintf(rankstr, sizeof(rankstr), "%u", procs[i].rank);
	    }

	    if (i == 0) {
		mlog("%s{%s", procs[i].nspace, rankstr);
		continue;
	    }

	    /* i > 0 */
	    if (PMIX_CHECK_NSPACE(procs[i].nspace, procs[i-1].nspace) == 0) {
		mlog(",%s", rankstr);
	    }
	    else {
		mlog("},%s{%s", procs[i].nspace, rankstr);
	    }
	}
	mlog("})\n");
    }

    /* handle command directives */
    for (size_t i = 0; i < ninfo; i++) {
	if (PMIX_CHECK_KEY(info+i, PMIX_COLLECT_DATA)) {
	    mlog("%s: Found %s info [key '%s' value '%s']\n", __func__,
		 info[i].key,
		 (info[i].flags & PMIX_INFO_REQD) ? "required" : "optional",
		 (PMIX_INFO_TRUE(&info[i])) ? "true" : "false");
	    continue;
	}

	/* inform about lacking implementation */
	mlog("%s: Ignoring info [key '%s' flags '%s' value.type '%s']"
	     " (not implemented)\n", __func__, info[i].key,
	     PMIx_Info_directives_string(info[i].flags),
	     PMIx_Data_type_string(info[i].value.type));
    }

    /* initialize return data struct */
    modexdata_t *mdata;
    mdata = umalloc(sizeof(*mdata));
    mdata->data = NULL;
    mdata->ndata = 0;
    mdata->cbfunc = cbfunc;
    mdata->cbdata = cbdata;

    /* XXX kind of a hack, can there be multiple nspaces involved? */
    strncpy(mdata->proc.nspace, procs[0].nspace, sizeof(mdata->proc.nspace));

    int ret;
    ret = pspmix_service_fenceIn(procs, nprocs, data, ndata, mdata);
    if (ret == -1) return PMIX_ERROR;
    if (ret == 0) return PMIX_SUCCESS;
    assert(ret == 1);

    ufree(mdata);
    mdbg(PSPMIX_LOG_FENCE, "%s: Returning PMIX_OPERATION_SUCCEEDED.\n",
	    __func__);
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
    ufree(mdata);
}

/* Return modex data. The returned blob contains the data from the
 * process requested got from the server of that process.
 *
 * This function takes the ownership of mdata, so no locking required */
void pspmix_server_returnModexData(bool success, modexdata_t *mdata)
{
    assert(mdata != NULL);

    mdbg(PSPMIX_LOG_CALL, "%s(success %d rank %u namespace %s ndata %lu)\n",
	 __func__, success, mdata->proc.rank, mdata->proc.nspace, mdata->ndata);

    pmix_status_t status;
    status = success ? PMIX_SUCCESS : PMIX_ERROR;

    /* Call the callback provided by the server in server_dmodex_req_cb().
     * As the data is "owned" by the host server, provide a secondary callback
     * function to notify the host server that we are done with the data so it
     * can be released */
    if (mdata->cbfunc) {
	mdata->cbfunc(status, mdata->data, mdata->ndata, mdata->cbdata,
		dmodex_req_release_fn, mdata);
    }
}

/* Used by the PMIx server to request its local host contact the
 * PMIx server on the remote node that hosts the specified proc to
 * obtain and return a direct modex blob for that proc.
 *
 * The array of info structs is used to pass user-requested options to the server.
 * This can include a timeout to preclude an indefinite wait for data that
 * may never become available. The directives are optional _unless_ the _mandatory_ flag
 * has been set - in such cases, the host RM is required to return an error
 * if the directive cannot be met. */
/* pmix_server_dmodex_req_fn_t */
static pmix_status_t server_dmodex_req_cb(const pmix_proc_t *proc,
					  const pmix_info_t info[],
					  size_t ninfo,
					  pmix_modex_cbfunc_t cbfunc,
					  void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s(rank %u namespace %s)\n", __func__, proc->rank,
	 proc->nspace);

    /* inform about lacking implementation */
    for (size_t i = 0; i < ninfo; i++) {
	mlog("%s: Ignoring info [key '%s' flags '%s' value.type '%s']"
	     " (not implemented)\n", __func__, info[i].key,
	     PMIx_Info_directives_string(info[i].flags),
	     PMIx_Data_type_string(info[i].value.type));
    }

    /* initialize return data struct */
    modexdata_t *mdata;
    mdata = umalloc(sizeof(*mdata));
    mdata->proc.rank = proc->rank;
    memcpy(mdata->proc.nspace, proc->nspace, PMIX_MAX_NSLEN);
    mdata->data = NULL;
    mdata->ndata = 0;
    mdata->cbfunc = cbfunc;
    mdata->cbdata = cbdata;

    if (!pspmix_service_sendModexDataRequest(mdata)) {
	mlog("%s: pspmix_service_sendModexDataRequest() for rank %u in"
	     " namespace %s failed.\n", __func__, proc->rank, proc->nspace);
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
static void requestModexData_cb(
	pmix_status_t status,
	char *data, size_t ndata,
	void *cbdata)
{
    modexdata_t *mdata;
    mdata = cbdata;

    mdbg(PSPMIX_LOG_CALL, "%s(ndata %zu rank %u namespace %s)\n", __func__,
	 ndata, mdata->proc.rank, mdata->proc.nspace);

    if (status == PMIX_SUCCESS) {
	mdata->data = data;
	mdata->ndata = ndata;
    }
    else {
	mlog("%s: modex data request for rank %u in namespace %s failed:"
	     " %s\n", __func__, mdata->proc.rank, mdata->proc.nspace,
	     PMIx_Error_string(status));
    }

    pspmix_service_sendModexDataResponse(status == PMIX_SUCCESS ? true : false,
	    mdata);
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

    pmix_status_t status;
    status = PMIx_server_dmodex_request(&mdata->proc, requestModexData_cb,
	    mdata);
    if (status != PMIX_SUCCESS) {
	mlog("%s: PMIx_server_dmodex_request() failed: %s\n", __func__,
	     PMIx_Error_string(status));
	return false;
    }

    return true;
}

/* Publish data per the PMIx API specification. The callback is to be executed
 * upon completion of the operation. The default data range is expected to be
 * PMIX_SESSION, and the default persistence PMIX_PERSIST_SESSION. These values
 * can be modified by including the respective pmix_info_t struct in the
 * provided array.
 *
 * Note that the host server is not required to guarantee support for any specific
 * range - i.e., the server does not need to return an error if the data store
 * doesn't support range-based isolation. However, the server must return an error
 * (a) if the key is duplicative within the storage range, and (b) if the server
 * does not allow overwriting of published info by the original publisher - it is
 * left to the discretion of the host server to allow info-key-based flags to modify
 * this behavior.
 *
 * The persistence indicates how long the server should retain the data.
 *
 * The identifier of the publishing process is also provided and is expected to
 * be returned on any subsequent lookup request */
/* pmix_server_publish_fn_t */
static pmix_status_t server_publish_cb(
	const pmix_proc_t *proc,
	const pmix_info_t info[], size_t ninfo,
	pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* TODO */
#if 0
    size_t i, errcount;
    bool ret;
    const char *encval;

    errcount = 0;
    for (i=0; i<ninfo; i++) {
	encval = encodeValue(&(info[i].value), proc->rank);
	ret = pspmix_service_putToKVS(proc->nspace, info[i].key, encval);
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
 * of string keys.
 *
 * The array of info structs is used to pass user-requested options to the server.
 * This can include a wait flag to indicate that the server should wait for all
 * data to become available before executing the callback function, or should
 * immediately callback with whatever data is available. In addition, a timeout
 * can be specified on the wait to preclude an indefinite wait for data that
 * may never be published. */
/* pmix_server_lookup_fn_t */
static pmix_status_t server_lookup_cb(
	const pmix_proc_t *proc, char **keys,
	const pmix_info_t info[], size_t ninfo,
	pmix_lookup_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* TODO */
#if 0
    const char *encval;
    size_t i, nkeys, ndata;
    pmix_pdata_t *data;

    /* inform about lacking implementation */
    for (i = 0; i < ninfo; i++) {
	mlog("%s: Ignoring info [key '%s' flags '%s' value.type '%s']"
	     " (not implemented)\n", __func__, info[i].key,
	     PMIx_Info_directives_string(info[i].flags),
	     PMIx_Data_type_string(info[i].value.type));
    }

    /* count keys */
    nkeys = 0;
    while(keys[nkeys] != NULL) nkeys++;

    /* create array of return data */
    PMIX_PDATA_CREATE(data, nkeys);

    ndata = 0;

    for (i = 0; i < nkeys; i++) {
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

/* Delete data from the data store. The host server will be passed a NULL-terminated array
 * of string keys, plus potential directives such as the data range within which the
 * keys should be deleted. The callback is to be executed upon completion of the delete
 * procedure */
/* pmix_server_unpublish_fn_t */
static pmix_status_t server_unpublish_cb(
	const pmix_proc_t *proc, char **keys,
	const pmix_info_t info[], size_t ninfo,
	pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* TODO */

    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Spawn a set of applications/processes as per the PMIx API. Note that
 * applications are not required to be MPI or any other programming model.
 * Thus, the host server cannot make any assumptions as to their required
 * support. The callback function is to be executed once all processes have
 * been started. An error in starting any application or process in this
 * request shall cause all applications and processes in the request to
 * be terminated, and an error returned to the originating caller.
 *
 * Note that a timeout can be specified in the job_info array to indicate
 * that failure to start the requested job within the given time should
 * result in termination to avoid hangs */
/* pmix_server_spawn_fn_t  */
static pmix_status_t server_spawn_cb(
	const pmix_proc_t *proc,
	const pmix_info_t job_info[], size_t ninfo,
	const pmix_app_t apps[], size_t napps,
	pmix_spawn_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* TODO */

    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Record the specified processes as "connected". This means that the resource
 * manager should treat the failure of any process in the specified group as
 * a reportable event, and take appropriate action. The callback function is
 * to be called once all participating processes have called connect. Note that
 * a process can only engage in *one* connect operation involving the identical
 * set of procs at a time. However, a process *can* be simultaneously engaged
 * in multiple connect operations, each involving a different set of procs
 *
 * Note also that this is a collective operation within the client library, and
 * thus the client will be blocked until all procs participate. Thus, the info
 * array can be used to pass user directives, including a timeout.
 * The directives are optional _unless_ the _mandatory_ flag
 * has been set - in such cases, the host RM is required to return an error
 * if the directive cannot be met. */
/* pmix_server_connect_fn_t */
static pmix_status_t server_connect_cb(
	const pmix_proc_t procs[], size_t nprocs,
	const pmix_info_t info[], size_t ninfo,
	pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* TODO */

    /* not implemented yet */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Disconnect a previously connected set of processes. An error should be returned
 * if the specified set of procs was not previously "connected". As above, a process
 * may be involved in multiple simultaneous disconnect operations. However, a process
 * is not allowed to reconnect to a set of ranges that has not fully completed
 * disconnect - i.e., you have to fully disconnect before you can reconnect to the
 * same group of processes.
  *
 * Note also that this is a collective operation within the client library, and
 * thus the client will be blocked until all procs participate. Thus, the info
 * array can be used to pass user directives, including a timeout.
 * The directives are optional _unless_ the _mandatory_ flag
 * has been set - in such cases, the host RM is required to return an error
 * if the directive cannot be met. */
/* pmix_server_disconnect_fn_t */
static pmix_status_t server_disconnect_cb(
	const pmix_proc_t procs[], size_t nprocs,
	const pmix_info_t info[], size_t ninfo,
	pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* TODO */

    /* not implemented yet */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Register to receive notifications for the specified events. The resource
 * manager is _required_ to pass along to the local PMIx server all events
 * that directly relate to a registered namespace. However, the RM may have
 * access to events beyond those - e.g., environmental events. The PMIx server
 * will register to receive environmental events that match specific PMIx
 * event codes. If the host RM supports such notifications, it will need to
 * translate its own internal event codes to fit into a corresponding PMIx event
 * code - any specific info beyond that can be passed in via the pmix_info_t
 * upon notification.
 *
 * The info array included in this API is reserved for possible future directives
 * to further steer notification.
 */
/* pmix_server_register_events_fn_t */
static pmix_status_t server_register_events_cb(
	pmix_status_t *codes, size_t ncodes,
	const pmix_info_t info[], size_t ninfo,
	pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* TODO ignoring this for the moment */

    /* not implemented yet */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Deregister to receive notifications for the specified environmental events
 * for which the PMIx server has previously registered. The host RM remains
 * required to notify of any job-related events */
/* pmix_server_deregister_events_fn_t */
static pmix_status_t server_deregister_events_cb(
	pmix_status_t *codes, size_t ncodes,
	pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* TODO ignoring this for the moment */

    /* not implemented yet */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

#if 0
/* Register a socket the host server can monitor for connection
 * requests, harvest them, and then call our internal callback
 * function for further processing. A listener thread is essential
 * to efficiently harvesting connection requests from large
 * numbers of local clients such as occur when running on large
 * SMPs. The host server listener is required to call accept
 * on the incoming connection request, and then passing the
 * resulting soct to the provided cbfunc. A NULL for this function
 * will cause the internal PMIx server to spawn its own listener
 * thread */
/* pmix_server_listener_fn_t */
static pmix_status_t
pspmix_server_listener_cb(int listening_sd,
	pmix_connection_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    /* not implemented */
    return PMIX_ERR_NOT_IMPLEMENTED;
}
#endif

/* Notify the specified processes of an event generated either by
 * the PMIx server itself, or by one of its local clients. The process
 * generating the event is provided in the source parameter. */
/* pmix_server_notify_event_fn_t */
static pmix_status_t server_notify_event_cb(
	pmix_status_t code, const pmix_proc_t *source,
	pmix_data_range_t range,
	pmix_info_t info[], size_t ninfo,
	pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* not implemented */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Query information from the resource manager. The query will include
 * the nspace/rank of the proc that is requesting the info, an
 * array of pmix_query_t describing the request, and a callback
 * function/data for the return. */
/* pmix_server_query_fn_t */
static pmix_status_t server_query_cb(
	pmix_proc_t *proc,
	pmix_query_t *queries, size_t nqueries,
	pmix_info_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s(rank %u namespace %s)\n", __func__, proc->rank,
	 proc->nspace);

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

/* Register that a tool has connected to the server, and request
 * that the tool be assigned an nspace/rank for further interactions.
 * The optional pmix_info_t array can be used to pass qualifiers for
 * the connection request:
 *
 * (a) PMIX_USERID - effective userid of the tool
 * (b) PMIX_GRPID - effective groupid of the tool
 * (c) PMIX_FWD_STDOUT - forward any stdout to this tool
 * (d) PMIX_FWD_STDERR - forward any stderr to this tool
 * (e) PMIX_FWD_STDIN - forward stdin from this tool to any
 *     processes spawned on its behalf
 */
/* pmix_server_tool_connection_fn_t */
static void server_tool_connection_cb(
	pmix_info_t *info, size_t ninfo,
	pmix_tool_connection_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* not implemented */
}

/* Log data on behalf of a client */
/* pmix_server_log_fn_t */
static void server_log_cb(
	const pmix_proc_t *client,
	const pmix_info_t data[], size_t ndata,
	const pmix_info_t directives[], size_t ndirs,
	pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);
}

/* Request allocation modifications on behalf of a client */
/* pmix_server_alloc_fn_t */
static pmix_status_t server_alloc_cb(
	const pmix_proc_t *client,
	pmix_alloc_directive_t directive,
	const pmix_info_t data[], size_t ndata,
	pmix_info_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* Execute a job control action on behalf of a client */
/* pmix_server_job_control_fn_t */
static pmix_status_t server_job_control_cb(
	const pmix_proc_t *requestor,
	const pmix_proc_t targets[], size_t ntargets,
	const pmix_info_t directives[], size_t ndirs,
	pmix_info_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* not implemented */
    return PMIX_ERR_NOT_IMPLEMENTED;
}


/* Request that a client be monitored for activity */
/* pmix_server_monitor_fn_t */
static pmix_status_t server_monitor_cb(
	const pmix_proc_t *requestor,
	const pmix_info_t *monitor, pmix_status_t error,
	const pmix_info_t directives[], size_t ndirs,
	pmix_info_cbfunc_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mlog("%s: NOT IMPLEMENTED\n", __func__);

    /* not implemented */
    return PMIX_ERR_NOT_IMPLEMENTED;
}

/* struct holding the server callback functions */
static pmix_server_module_t module = {
    /* v1x interfaces */
    .client_connected = server_client_connected_cb,
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
};

/* XXX */
static void errhandler(
	size_t evhdlr_registration_id, pmix_status_t status,
	const pmix_proc_t *source,
	pmix_info_t info[], size_t ninfo,
	pmix_info_t results[], size_t nresults,
	pmix_event_notification_cbfunc_fn_t cbfunc, void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s(status %d proc %s:%u ninfo %lu nresults %lu\n",
	 __func__, status, source->nspace, source->rank, ninfo, nresults);
}

/**
 * To be called by error handler registration function to provide success state
 */
static void registerErrorHandler_cb (
	pmix_status_t status, size_t errhandler_ref,
	void *cbdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    mycbdata_t *data;
    data = cbdata;

    data->status = status;

    SET_CBDATA_AVAIL(data);
}

bool pspmix_server_init(uint32_t uid, uint32_t gid)
{
    mdbg(PSPMIX_LOG_CALL, "%s(uid %u gid %u)\n", __func__, uid, gid);

    /* print some interesting environment variables if set */
    for (size_t i = 0; environ[i]; i++) {
	if (!strncmp(environ[i], "PMIX_DEBUG", 10)
	    || !strncmp (environ[i], "PMIX_OUTPUT", 11)
	    || !strncmp (environ[i], "PMIX_MCA_", 9)) {
	    mlog("%s: %s set\n", __func__, environ[i]);
	}
    }

    mycbdata_t cbdata;
    INIT_CBDATA(cbdata);
    cbdata.ninfo = 2;
    PMIX_INFO_CREATE(cbdata.info, cbdata.ninfo);

    PMIX_INFO_LOAD(&cbdata.info[0], PMIX_USERID, &uid, PMIX_UINT32);
    PMIX_INFO_LOAD(&cbdata.info[1], PMIX_GRPID, &gid, PMIX_UINT32);

    mdbg(PSPMIX_LOG_VERBOSE, "%s: Setting uid %u gid %u\n", __func__, uid, gid);

    /* initialize server library */
    pmix_status_t status;
    status = PMIx_server_init(&module, cbdata.info, cbdata.ninfo);
    if (status != PMIX_SUCCESS) {
	mlog("%s: PMIx_server_init() failed: %s\n", __func__,
	     PMIx_Error_string(status));
	return false;
    }
    mdbg(PSPMIX_LOG_VERBOSE, "%s: PMIx_server_init() successful\n", __func__);
    DESTROY_CBDATA(cbdata);

    /* register the error handler */
    INIT_CBDATA(cbdata);
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
			tmp = getAllocatedInfoString(&data.info[i]);
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
    mycbdata_t *data;
    data = provided_cbdata;

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

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

    char *ret, *ptr;
    size_t i, s, retsize, rest;

    retsize = CHUNK;
    ret = umalloc(retsize * sizeof(*ret));

    ptr = ret;
    rest = retsize;

    for (i = 1; i <= len; i++) {
	s = snprintf(ptr, rest, "%d", array[i-1]);
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
    PspmixProcess_t *proc;
    list_for_each(n, procMap) {
	PspmixNode_t *node;
	node = list_entry(n, PspmixNode_t, next);
	for(size_t i = 0; i < node->procs.len; i++) {
	    proc = vectorGet(&node->procs, i, PspmixProcess_t);
	    sprintf(buf, "%u", proc->rank);
	    if (pmap.len > 0) charvAdd(&pmap, &ranksep);
	    charvAddCount(&pmap, buf, strlen(buf));
	}
	if (node->next.next != procMap) charvAdd(&pmap, &nodesep);
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

    for(size_t i = 0; i < node->procs.len; i++) {
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
    pmix_info_t *infos;

#define SESSION_INFO_ARRAY_LEN 2

    PMIX_INFO_CREATE(infos, SESSION_INFO_ARRAY_LEN);

    /* first entry needs to be session id */
    PMIX_INFO_LOAD(&infos[0], PMIX_SESSION_ID, &session_id, PMIX_UINT32);

    /* number of slots in this session */
    PMIX_INFO_LOAD(&infos[1], PMIX_MAX_PROCS, &universe_size, PMIX_UINT32);

    /* XXX omitting PMIX_NUM_NODES (PMIX_UINT32) */

    /* XXX omitting PMIX_NODE_MAP (char* created by PMIx_generate_regex) */

#if PRINT_FILLINFOS
    mlog("%s: %s(%d)=%u - %s(%d)=%u\n", __func__,
	 infos[0].key, infos[0].value.type, infos[0].value.data.uint32,
	 infos[1].key, infos[1].value.type, infos[1].value.data.uint32);
#endif

    sessionInfo->type = PMIX_INFO;
    sessionInfo->size = SESSION_INFO_ARRAY_LEN;
    sessionInfo->array = infos;
}

static void fillJobInfoArray(pmix_data_array_t *jobInfo, const char *jobId,
	uint32_t jobSize, uint32_t maxProcs, const char *nodelist_s,
	list_t *procMap)
{
    pmix_info_t *infos;

#define JOB_INFO_ARRAY_LEN 5

    PMIX_INFO_CREATE(infos, JOB_INFO_ARRAY_LEN);

    /* job identifier (this is the name of the namespace */
    strncpy(infos[0].key, PMIX_JOBID, PMIX_MAX_KEYLEN);
    PMIX_VAL_SET(&infos[0].value, string, jobId);

    /* total num of processes in this job across all contained applications */
    PMIX_INFO_LOAD(&infos[1], PMIX_JOB_SIZE, &jobSize, PMIX_UINT32);

    /* Maximum number of processes in this job */
    PMIX_INFO_LOAD(&infos[2], PMIX_MAX_PROCS, &maxProcs, PMIX_UINT32);

    /* regex of nodes containing procs for this job */
    char *nodelist_r;
    PMIx_generate_regex(nodelist_s, &nodelist_r);
    strncpy(infos[3].key, PMIX_NODE_MAP, PMIX_MAX_KEYLEN);
    PMIX_VAL_ASSIGN(&infos[3].value, string, nodelist_r);

    /* regex describing procs on each node within this job */
    char *pmap_s;
    pmap_s = getProcessMapString(procMap);

#if PRINT_FILLINFOS
    mlog("%s: proc_map string created: '%s'\n", __func__, pmap_s);
#endif
    char *pmap_r;
    PMIx_generate_ppn(pmap_s, &pmap_r);
    ufree(pmap_s);
    strncpy(infos[4].key, PMIX_PROC_MAP, PMIX_MAX_KEYLEN);
    PMIX_VAL_ASSIGN(&infos[4].value, string, pmap_r);

    /* optional infos (PMIx v3.0):
     * * PMIX_SERVER_NSPACE "pmix.srv.nspace" (char*)
     *     Name of the namespace to use for this PMIx server.
     *
     * * PMIX_SERVER_RANK "pmix.srv.rank" (pmix_rank_t)
     *     Rank of this PMIx server
     *
     * * PMIX_NPROC_OFFSET "pmix.offset" (pmix_rank_t)
     *     Starting global rank of this job
     *
     * * PMIX_ALLOCATED_NODELIST "pmix.alist" (char*)
     *     Comma-delimited list of all nodes in this allocation regardless of
     *     whether or not they currently host processes
     *
     * * PMIX_JOB_NUM_APPS "pmix.job.napps" (uint32_t)
     *     Number of applications in this job.
     *
     * * PMIX_MAPBY "pmix.mapby" (char*)
     *     Process mapping policy
     *
     * * PMIX_RANKBY "pmix.rankby" (char*)
     *     Process ranking policy
     *
     * * PMIX_BINDTO "pmix.bindto" (char*)
     *     Process binding policy
     */

#if PRINT_FILLINFOS
    mlog("%s: %s(%d)='%s' - %s(%d)=%u - %s(%d)=%u - %s(%d)='%s' - "
	 "%s(%d)='%s'\n", __func__,
	 infos[0].key, infos[0].value.type, infos[0].value.data.string,
	 infos[1].key, infos[1].value.type, infos[1].value.data.uint32,
	 infos[2].key, infos[2].value.type, infos[2].value.data.uint32,
	 infos[3].key, infos[3].value.type, infos[3].value.data.string,
	 infos[4].key, infos[4].value.type, infos[4].value.data.string);
#endif

    jobInfo->type = PMIX_INFO;
    jobInfo->size = JOB_INFO_ARRAY_LEN;
    jobInfo->array = infos;
}

static void fillAppInfoArray(pmix_data_array_t *appInfo, PspmixApp_t *app)
{
    pmix_info_t *infos;

#define APP_INFO_ARRAY_LEN 3

    PMIX_INFO_CREATE(infos, APP_INFO_ARRAY_LEN);

    /* application number */
    PMIX_INFO_LOAD(&infos[0], PMIX_APPNUM, &app->num, PMIX_UINT32);

    /* number of processes in this application */
    PMIX_INFO_LOAD(&infos[1], PMIX_APP_SIZE, &app->size, PMIX_UINT32);

    /* lowest rank in this application within the job */
    PMIX_INFO_LOAD(&infos[2], PMIX_APPLDR, &app->firstRank, PMIX_PROC_RANK);

#if PRINT_FILLINFOS
    mlog("%s: %s(%d)='%u' - %s(%d)=%u - %s(%d)=%u\n", __func__,
	 infos[0].key, infos[0].value.type, infos[0].value.data.uint32,
	 infos[1].key, infos[1].value.type, infos[1].value.data.uint32,
	 infos[2].key, infos[2].value.type, infos[2].value.data.rank);
#endif

    appInfo->type = PMIX_INFO;
    appInfo->size = APP_INFO_ARRAY_LEN;
    appInfo->array = infos;
}

static void fillProcDataArray(pmix_data_array_t *procData,
	PspmixProcess_t *proc, PSnodes_ID_t nodeID, bool spawned)
{
    uint32_t val_u32;

    pmix_info_t *infos;

#define PROC_DATA_ARRAY_LEN 8

    PMIX_INFO_CREATE(infos, PROC_DATA_ARRAY_LEN);

    /* first entry needs to be the rank */
    PMIX_INFO_LOAD(&infos[0], PMIX_RANK, &proc->rank, PMIX_PROC_RANK);

    /* rank on this node within this job */
    PMIX_INFO_LOAD(&infos[1], PMIX_LOCAL_RANK, &proc->lrank, PMIX_UINT16);

    /* rank on this node spanning all jobs */
    PMIX_INFO_LOAD(&infos[2], PMIX_NODE_RANK, &proc->nrank, PMIX_UINT16);

    /* node identifier where the specified proc is located */
    val_u32 = nodeID;
    PMIX_INFO_LOAD(&infos[3], PMIX_NODEID, &val_u32, PMIX_UINT32);

    /* app number within the job */
    PMIX_INFO_LOAD(&infos[4], PMIX_APPNUM, &proc->app->num, PMIX_UINT32);

    /* rank within this app */
    PMIX_INFO_LOAD(&infos[5], PMIX_APP_RANK, &proc->arank, PMIX_PROC_RANK);

    /* rank spanning across all jobs in this session */
    PMIX_INFO_LOAD(&infos[6], PMIX_GLOBAL_RANK, &proc->grank, PMIX_PROC_RANK);

    /* true if this proc resulted from a call to PMIx_Spawn */
    PMIX_INFO_LOAD(&infos[7], PMIX_SPAWNED, &spawned, PMIX_BOOL);

    /* optional infos (PMIx v3.0):
     * * PMIX_PROCID "pmix.procid" (pmix_proc_t)
     *     Process identifier
     *
     * * PMIX_GLOBAL_RANK "pmix.grank" (pmix_rank_t)
     *     Process rank spanning across all jobs in this session.
     *
     * * PMIX_HOSTNAME "pmix.hname" (char*)
     *     Name of the host where the specified process is running.
     */

#if PRINT_FILLINFOS
    mlog("%s: %s(%d)=%u - %s(%d)=%hu - %s(%d)=%hu - %s(%d)=%u - %s(%d)=%u -"
	 " %s(%d)=%hu - %s(%d)=%u - %s(%d)=%d\n", __func__,
	 infos[0].key, infos[0].value.type, infos[0].value.data.rank,
	 infos[1].key, infos[1].value.type, infos[1].value.data.uint16,
	 infos[2].key, infos[2].value.type, infos[2].value.data.uint16,
	 infos[3].key, infos[3].value.type, infos[3].value.data.uint32,
	 infos[4].key, infos[4].value.type, infos[4].value.data.uint32,
	 infos[5].key, infos[5].value.type, infos[5].value.data.rank,
	 infos[6].key, infos[6].value.type, infos[6].value.data.rank,
	 infos[7].key, infos[7].value.type, infos[7].value.data.flag);
#endif

    procData->type = PMIX_INFO;
    procData->size = PROC_DATA_ARRAY_LEN;
    procData->array = infos;
}

/**
 * To be called by PMIx_register_namespace() to provide status
 */
static void registerNamespace_cb(pmix_status_t status, void *cbdata)
{
    mycbdata_t *data;
    data = cbdata;

    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    data->status = status;

    SET_CBDATA_AVAIL(data);
}

bool pspmix_server_registerNamespace(
	const char *nspace, uint32_t sessionId,	uint32_t univSize,
	uint32_t jobSize, bool spawned, const char *nodelist_s,
	list_t *procMap, uint32_t numApps, PspmixApp_t *apps,
	PSnodes_ID_t nodeID)
{
    mdbg(PSPMIX_LOG_CALL, "%s(nspace '%s' sessionId %u univSize %u jobSize %u"
	 " spawned %d nodelist_s '%s' numApps '%u' nodeID %hd)\n", __func__,
	 nspace, sessionId, univSize, jobSize, spawned, nodelist_s, numApps,
	 nodeID);

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
    PspmixNode_t *mynode;
    mynode = findNodeInList(nodeID, procMap);
    if (mynode == NULL) {
	mlog("%s: Could not find my own node (%u) in process map\n", __func__,
	     nodeID);
	return false;
    }

    uint32_t val_u32;
    size_t count;

    /* fill infos */
    mycbdata_t data;
    INIT_CBDATA(data);
    data.ninfo = 4 + numApps + jobSize + 2;

    PMIX_INFO_CREATE(data.info, data.ninfo);
    count = 0;

    /* ===== session info ===== */

    /* number of allocated slots in a session (here for historical reasons) */
    PMIX_INFO_LOAD(&data.info[count], PMIX_UNIV_SIZE, &univSize, PMIX_UINT32);
    ++count;

    /* session info array */
    pmix_data_array_t sessionInfo;
    fillSessionInfoArray(&sessionInfo, sessionId, univSize);

    PMIX_INFO_LOAD(&data.info[count], PMIX_SESSION_INFO_ARRAY, &sessionInfo,
	    PMIX_DATA_ARRAY);
    ++count;

    /* ===== job info array ===== */
    /* total num of processes in this job (here for historical reasons) */
    PMIX_INFO_LOAD(&data.info[count], PMIX_JOB_SIZE, &jobSize, PMIX_UINT32);
    ++count;

    pmix_data_array_t jobInfo;
    fillJobInfoArray(&jobInfo, nspace, jobSize, univSize, nodelist_s,
	    procMap);

    PMIX_INFO_LOAD(&data.info[count], PMIX_JOB_INFO_ARRAY, &jobInfo,
	    PMIX_DATA_ARRAY);
    ++count;

    /* ===== application info arrays ===== */
    for (uint32_t i = 0; i < numApps; i++) {
	pmix_data_array_t appInfo;
	fillAppInfoArray(&appInfo, apps+i);

	PMIX_INFO_LOAD(&data.info[count], PMIX_APP_INFO_ARRAY, &appInfo,
		PMIX_DATA_ARRAY);
	++count;
    }

    /* ===== process data ===== */

    /* information about all global ranks */
    list_t *n;
    list_for_each(n, procMap) {
	PspmixNode_t *node;
	node = list_entry(n, PspmixNode_t, next);
	for(size_t i = 0; i < node->procs.len; i++) {
	    PspmixProcess_t *proc;
	    proc = vectorGet(&node->procs, i, PspmixProcess_t);

	    pmix_data_array_t procData;
	    fillProcDataArray(&procData, proc, node->id, spawned);

	    PMIX_INFO_LOAD(&data.info[count], PMIX_PROC_DATA, &procData,
		    PMIX_DATA_ARRAY);
	    ++count;
	}
    }

    /* ===== own node info ===== */

    /* number of processes in this job/namespace on this node */
    val_u32 = mynode->procs.len;
    PMIX_INFO_LOAD(&data.info[count], PMIX_LOCAL_SIZE, &val_u32, PMIX_UINT32);
    ++count;

    /* comma-delimited string of ranks on this node within the specified job */
    char *lpeers;
    lpeers = getNodeRanksString(mynode);
    if (lpeers[0] == '\0') {
	mlog("%s: no local ranks found.\n", __func__);
	ufree(lpeers);
	DESTROY_CBDATA(data);
	return false;
    }
#if PRINT_FILLINFOS
    mlog("%s: local ranks string created: '%s'\n", __func__, lpeers);
#endif
    PMIX_INFO_LOAD(&data.info[count], PMIX_LOCAL_PEERS, lpeers, PMIX_STRING);
    ++count;
    ufree(lpeers);

    /* XXX omitting PMIX_LOCAL_CPUSETS for now which is required according to
     * the standard */

    /* optional infos (PMIx v3.0):
     * * PMIX_AVAIL_PHYS_MEMORY "pmix.pmem" (uint64_t)
     *     Total available physical memory on this node.
     *
     * * PMIX_HWLOC_XML_V1 "pmix.hwlocxml1" (char*)
     *     XML representation of local topology using HWLOC’s v1.x format.
     *
     * * PMIX_HWLOC_XML_V2 "pmix.hwlocxml2" (char*)
     *     XML representation of local topology using HWLOC’s v2.x format.
     *
     * * PMIX_LOCALLDR "pmix.lldr" (pmix_rank_t)
     *     Lowest rank on this node within this job
     *
     *  * PMIX_NODE_SIZE "pmix.node.size" (uint32_t)
     *     Number of processes across all jobs on this node.
     *
     * * PMIX_LOCAL_PROCS "pmix.lprocs" (pmix_proc_t array)
     *     Array of pmix_proc_t of all processes on the specified node
     */



#if 0 /* XXX: are some of these needed even if not mentioned in PMIx v3.0??? */
    ++count;
    /* user id of the job */
    PMIX_INFO_LOAD(&data.info[count], PMIX_USERID, &uid, PMIX_UINT32);

    ++count;
    /* group id of the job */
    PMIX_INFO_LOAD(&data.info[count], PMIX_GRPID, &gid, PMIX_UINT32);

    ++count;
    /* node identifier */
    val_u32 = nodeID;
    PMIX_INFO_LOAD(&data.info[count], PMIX_NODEID, &val_u32, PMIX_UINT32);

    ++count;
    /* lowest rank on this node within this job */
    proc = vectorGet(&mynode->procs, 0, PspmixProcess_t);
    PMIX_INFO_LOAD(&data.info[count], PMIX_LOCALLDR, &proc->applead,
	    PMIX_PROC_RANK);
#endif

    if (count != data.ninfo) {
	mlog("%s: WARNING: Number of info fields does not match (%lu != %lu)\n",
	     __func__, count, data.ninfo);
    }

    /* debugging output of info values */
    if (mset(PSPMIX_LOG_VERBOSE)) {
	vector_t infostr;
	charvInit(&infostr, 1024);

	char prefix[50];
	snprintf(prefix, 50, "%s:   ", __func__);

	for (size_t i = 0; i < data.ninfo; i++) {
	    char *tmpstr;
	    switch(PMIx_Data_print(&tmpstr, prefix, &data.info[i], PMIX_INFO)) {
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
#if PRINT_FILLINFOS
	mlog("%s: info array to be passed to PMIx_server_register_nspace():\n"
	     "%s", __func__, (char *)infostr.data);
#endif
	charvDestroy(&infostr);
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
    if (status != PMIX_SUCCESS) {
	errstr = PMIx_Error_string(status);
    }

    pspmix_service_destroyNamespace(cbdata, (status != PMIX_SUCCESS), errstr);
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
    mycbdata_t *data;
    data = cbdata;

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
    INIT_CBDATA(cbdata);
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
    mycbdata_t *data;
    data = cbdata;

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
    PMIX_PROC_LOAD(&proc, nspace, rank);

    /* register clients uid and gid as well as ident object */
    mycbdata_t cbdata;
    INIT_CBDATA(cbdata);
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

/* run this function in exception case to remove all client information  */
void pspmix_server_deregisterClient(const char *nspace, int rank)
{
    mdbg(PSPMIX_LOG_CALL, "%s(nspace '%s' rank %d)\n", __func__, nspace, rank);

    /* setup process struct */
    pmix_proc_t proc;
    PMIX_PROC_LOAD(&proc, nspace, rank);

    /* deregister client */
    mycbdata_t cbdata;
    INIT_CBDATA(cbdata);
    PMIx_server_deregister_client(&proc, NULL, NULL);
    PMIX_PROC_DESTRUCT(&proc);
}

/* get the client environment set */
bool pspmix_server_setupFork(const char *nspace, int rank, char ***childEnv)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    pmix_status_t status;

    /* setup process struct */
    pmix_proc_t proc;
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
