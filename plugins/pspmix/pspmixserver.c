/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
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
#include <pthread.h>

#include <pmix_server.h>
#include <pmix.h>

#include <hwloc.h>

#include "list.h"
#include "timer.h"
#include "pluginmalloc.h"
#include "pluginvector.h"
#include "pluginhelper.h"
#include "pscpu.h"
#include "psstrbuf.h"

#include "psidnodes.h"

#include "pspmixlog.h"
#include "pspmixservice.h"
#include "pspmixtypes.h"
#include "pspmixutil.h"

/* PMIx' return code change on the move to version 4 of the standard */
#define __PSPMIX_NOT_IMPLEMENTED PMIX_ERR_NOT_SUPPORTED

/* allow walking throu the environment */
extern char **environ;

/** Initialisation flag */
static bool initialized = false;

/** Generic type for callback data */
typedef struct {
    pmix_status_t status;
    volatile bool filled;
    pmix_info_t *info; /* @todo check using pmix_data_array_t */
    size_t ninfo;
} mycbdata_t;

/** Setting up data for callback routines */
#define INIT_CBDATA(d, n) do {		    \
    memset(&(d), 0, sizeof(d));		    \
    (d).ninfo = n;			    \
    if (n) PMIX_INFO_CREATE((d).info, n);   \
} while(0)

/** Waiting for data to be filled by callback function */
#define WAIT_FOR_CBDATA(d) while(!(d).filled) usleep(10)

/** Set data to be available (used in callback function) */
#define SET_CBDATA_AVAIL(d) (d)->filled = true

/** Setting up data for callback routines */
#define DESTROY_CBDATA(d) if ((d).ninfo) PMIX_INFO_FREE((d).info, (d).ninfo)

/** Generic type for callback function */
typedef struct {
    pmix_op_cbfunc_t cbfunc;
    void *cbdata;
    pmix_status_t status;
} mycbfunc_t;

/** Setting up callback function object */
#define INIT_CBFUNC(o, f, d) do {  \
    (o) = ucalloc(sizeof(*o));     \
    (o)->cbfunc = (f);             \
    (o)->cbdata = (d);             \
} while(0)

/** Setting up data for callback routines */
#define DESTROY_CBFUNC(d) do {  \
    ufree(d);                   \
} while (0)

/** Wrapper for PMIx_Info_list_add() with error checking */
#define INFO_LIST_ADD(i, key, val, t)					    \
    do {								    \
	pmix_status_t status = PMIx_Info_list_add(i, key, val, t);	    \
	if (status != PMIX_SUCCESS) flog("failed to add %s: %s\n", #key,    \
					 PMIx_Error_string(status));	    \
    } while(0)

/** Wrapper for PMIx_Info_list_convert() with error checking, frees source */
#define INFO_LIST_CONVERT(list, array)					    \
    do {								    \
	pmix_status_t status = PMIx_Info_list_convert(list, array);	    \
	PMIx_Info_list_release(list);					    \
	if (status != PMIX_SUCCESS) {					    \
	    flog("failed to convert info list: %s\n",			    \
		 PMIx_Error_string(status));				    \
	} \
    } while(0)

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
	    flog("unsupported value type\n");
	    *buffer = '\0';
    }

    if (res >= SBUFSIZE) {
	flog("BUFFER SIZE TOO SMALL!!!\n");
	*buffer = '\0';
    }

    return buffer;
}

/* Fill a typed pmix value from its string representation */
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
	flog("wrong format encval: no colon found\n");
	/* return undef */
	return;
    }

    /* search second colon */
    ptr = strchr(++ptr, ':');

    if (!ptr) {
	flog("wrong format encval: no second colon found\n");
	/* return undef */
	return;
    }

    /* terminate rank:type string */
    *ptr='\0';

    if (sscanf(buffer, "%d:%8s", rank, type) != 2) {
	flog("wrong format encval '%s': failed to scan rank:type string '%s'\n",
	     encval, buffer);
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
	flog("unknown type '%s' found in encval\n", type);
	/* return undef */
	return;
    }

    if (val->type == PMIX_UNDEF) {
	flog("could not parse value string '%s' (type '%s') in encval '%s'\n",
	     ptr, type, encval);
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
static pmix_status_t server_client_connected2_cb(const pmix_proc_t *proc,
						 void *clientObject,
						 pmix_info_t info[],
						 size_t ninfo,
						 pmix_op_cbfunc_t cbfunc,
						 void *cbdata)
{
    fdbg(PSPMIX_LOG_CALL, "proc %s clientObject %p cbfunc %p cbdata %p\n",
	 pmixProcStr(proc), clientObject, cbfunc, cbdata);

    mycbfunc_t *cb = NULL;
    if (cbfunc) INIT_CBFUNC(cb, cbfunc, cbdata);

    if (!pspmix_service_clientConnected(proc->nspace, clientObject, cb)) {
	DESTROY_CBFUNC(cb);
	return PMIX_ERROR;
    }

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
    fdbg(PSPMIX_LOG_CALL, "proc %s clientObject %p cbfunc %p cbdata %p\n",
	 pmixProcStr(proc), clientObject, cbfunc, cbdata);

    mycbfunc_t *cb = NULL;
    if (cbfunc) INIT_CBFUNC(cb, cbfunc, cbdata);

    if (!pspmix_service_clientFinalized(proc->nspace, clientObject, cb)) {
	DESTROY_CBFUNC(cbfunc);
	return PMIX_ERROR;
    }

    /* tell the server library to wait for the callback call */
    return PMIX_SUCCESS;
}

/* main function for callback threads */
static void * callcb(void *cb)
{
    mycbfunc_t *callback = cb;

    fdbg(PSPMIX_LOG_CALL, "status %s cbfunc %p cbdata %p\n",
	 PMIx_Error_string(callback->status), callback->cbfunc, callback->cbdata);

    callback->cbfunc(callback->status, callback->cbdata);

    DESTROY_CBFUNC(callback);

    return NULL;
}

void pspmix_server_operationFinished(pmix_status_t status, void* cb)
{
    fdbg(PSPMIX_LOG_CALL, "status %s cb %p\n", PMIx_Error_string(status), cb);
    /* check if the server library does provide a callback function */
    if (!cb) return;

    mycbfunc_t *callback = cb;

    callback->status = status;

    /* create throw away thread to call callback, ownership of the "callback"
     * object is transfered to that thread and it is responsible to destroy
     * it at the end. It is not allowed to do any modifications of that object
     * beside setting the volatile field "ready" to true. */
    int ret;
    pthread_attr_t attr;
    if ((ret = pthread_attr_init(&attr))) {
	fwarn(ret, "failed to create attr");
	DESTROY_CBFUNC(callback);
	return;
    }
    if ((ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))) {
	fwarn(ret, "failed to set detached state attr");
	pthread_attr_destroy(&attr);
	DESTROY_CBFUNC(callback);
	return;
    }

    pthread_t cbthread;
    if ((ret = pthread_create(&cbthread, &attr, callcb, callback))) {
	fwarn(ret, "failed to create callback thread");
	pthread_attr_destroy(&attr);
	DESTROY_CBFUNC(callback);
	return;
    }

    pthread_attr_destroy(&attr);
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
    fdbg(PSPMIX_LOG_CALL, "proc %s clientObject %p status %d nprocs %zd"
	 " cbdata %p\n", pmixProcStr(proc), clientObject, status, nprocs,
	 cbdata);

    flog("abort request by %s for ", pmixProcStr(proc));
    if (!procs || (nprocs == 1
		   && PMIX_CHECK_NSPACE(procs[0].nspace, proc->nspace)
		   && procs[0].rank == PMIX_RANK_WILDCARD)) {
	mlog("the whole namespace\n");
    } else {
	for (size_t i = 0; i < nprocs; i++) {
	    mlog(" %s%s", i ? "," : "", pmixProcStr(&proc[i]));
	}
	mlog(" (not supported)\n");

	// we do currently not support aborting subsets of namespaces
	return PMIX_ERR_PARAM_VALUE_NOT_SUPPORTED;
    }

    pspmix_service_abort(proc->nspace, msg, clientObject);

    return PMIX_OPERATION_SUCCEEDED;
}

/* free everything to be freed in fence stuff */
static void fencenb_release_fn(void *cbdata)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    modexdata_t *mdata = cbdata;

    ufree(mdata->data);
    ufree(mdata);
}

/* tell server helper library about fence finished
 *
 * This takes over ownership of @a mdata, thus, ensure it is dynamically
 * allocated using umalloc(). Of course the same holds for mdata->data.
 * This function uses the fields data, ndata, cbfunc, and cbdata of mdata.
 * It is fine for each of them to be NULL resp. 0, but if data is set, cbfunc
 * has to be set, too.
 */
void pspmix_server_fenceOut(bool success, modexdata_t *mdata)
{
    assert(mdata != NULL);
    assert(mdata->cbfunc != NULL || mdata->ndata == 0);

    /* do some early cleanup */
    if (!mdata->ndata) {
	ufree(mdata->data);
	mdata->data = NULL;
    }

    fdbg(PSPMIX_LOG_CALL, "success %s mdata->cbfunc %s mdata->ndata %lu\n",
	 success ? "true" : "false",
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

    if (mset(PSPMIX_LOG_CALL|PSPMIX_LOG_FENCE)) {
	flog("processes ");
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
	mlog("}\n");
    }

    /* handle command directives */
    for (size_t i = 0; i < ninfo; i++) {
	/* This is required to be supported by PMIx 4.1 standard.
	 * The PMIx server flags that all local data is collected and
	 * passed as a single blob in data. The host server is
	 * expected to pass data around and collect all blobs in the
	 * fence. Since this host-server implementation passes all
	 * data around anyhow, nothing to do about this info being set
	 * or not. */
	if (PMIX_CHECK_KEY(info+i, PMIX_COLLECT_DATA)) {
	    fdbg(PSPMIX_LOG_FENCE, "found %s info [key '%s' value '%s']\n",
		 (PMIX_INFO_IS_REQUIRED(&info[i])) ? "required" : "optional",
		 info[i].key,
		 (PMIX_INFO_TRUE(&info[i])) ? "true" : "false");
	    continue;
	}

	/* This is not part of PMIx 4.1 standard but is used by OpenPMIx 4.
	 * Currently it is included as provisional in the PMIx
	 * standard HEAD. It shall be used by the host server to tell
	 * other partners in the fence on the local status in order to
	 * report there accordingly, i.e. pass a return code different
	 * from PMIX_SUCCESS to the cbfunc. For the time being
	 * OpenPMIx has no means to have PMIX_LOCAL_COLLECTIVE_STATUS
	 * different from PMIX_SUCCESS. @todo implement this! */
	if (PMIX_CHECK_KEY(info+i, PMIX_LOCAL_COLLECTIVE_STATUS)) {
	    fdbg(PSPMIX_LOG_FENCE, "found %s info [key '%s' value '%s']\n",
		 (PMIX_INFO_IS_REQUIRED(&info[i])) ? "required" : "optional",
		 info[i].key,
		 PMIx_Error_string(info[i].value.data.status));
	    if (info[i].value.data.status != PMIX_SUCCESS) {
		flog("local collective error status passed: %s\n",
		     PMIx_Error_string(info[i].value.data.status));
	    }
	    continue;
	}

	/* This is not part of any PMIx standard version, but used by
	 * openPMIx since 4.2.1.
	 * https://github.com/openpmix/openpmix/commit/e66c29b93
	 * We cannot profit from sorted procs since our algorithm just
	 * acts on nodes and there is no guarantee that proc ordering
	 * transfers to node ordering. Thus, just ignore this info. */
	if (PMIX_CHECK_KEY(info+i, PMIX_SORTED_PROC_ARRAY)) {
	    fdbg(PSPMIX_LOG_FENCE, "found %s info [key '%s' value '%s']\n",
		 (PMIX_INFO_IS_REQUIRED(&info[i])) ? "required" : "optional",
		 info[i].key,
		 (PMIX_INFO_TRUE(&info[i])) ? "true" : "false");
	    continue;
	}

	/* inform about lacking implementation */
	flog("ignoring info [key '%s' flags '%s' value.type '%s']"
	     " (not implemented)\n", info[i].key,
	     PMIx_Info_directives_string(info[i].flags),
	     PMIx_Data_type_string(info[i].value.type));
    }

    /* initialize return data struct */
    modexdata_t *mdata = ucalloc(sizeof(*mdata));
    mdata->cbfunc = cbfunc;
    mdata->cbdata = cbdata;

    /* Advice to PMIx server hosts in PMIx standard 4.1 says: "The host is
     * responsible for free’ing the data object passed to it by the PMIx server
     * library."
     * So we pass ownership of data to the service module here */
    int ret = pspmix_service_fenceIn(procs, nprocs, data, ndata, mdata);
    fdbg(PSPMIX_LOG_FENCE, "fenceIn result: %d\n", ret);
    if (ret == -1) return PMIX_ERROR;
    if (ret == 0) return PMIX_SUCCESS;
    assert(ret == 1);

    ufree(mdata->data);
    ufree(mdata);
    fdbg(PSPMIX_LOG_FENCE, "return PMIX_OPERATION_SUCCEEDED\n");
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
    strvDestroy(mdata->reqKeys);
    ufree(mdata);
}

/* Return modex data. The returned blob contains the data from the
 * process requested got from the server of that process.
 *
 * This function takes the ownership of mdata, so no locking required */
void pspmix_server_returnModexData(pmix_status_t status, modexdata_t *mdata)
{
    assert(mdata != NULL);

    fdbg(PSPMIX_LOG_CALL, "status %s proc %s ndata %lu\n",
	 PMIx_Error_string(status), pmixProcStr(&mdata->proc), mdata->ndata);

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
    fdbg(PSPMIX_LOG_CALL, "proc %s\n", pmixProcStr(proc));

    strv_t reqKeys = strvNew(NULL);
    int timeout = 0;

    /* handle command directives */
    for (size_t i = 0; i < ninfo; i++) {
	const pmix_info_t *this = info + i;

	/* debug print each info */
	if (mset(PSPMIX_LOG_MODEX)) {
	    char *iStr = PMIx_Info_string(this);
	    flog("info[%zd] '%s'\n", i, iStr);
	    free(iStr);
	}

	/* support mandatory key PMIX_REQUIRED_KEY */
	if (PMIX_CHECK_KEY(this, PMIX_REQUIRED_KEY)) {
	    strvAdd(reqKeys, this->value.data.string);
	    continue;
	}

	/* support optional key PMIX_TIMEOUT */
	if (PMIX_CHECK_KEY(this, PMIX_TIMEOUT)) {
	    timeout = this->value.data.integer;
	    continue;
	}

	/* ignore keys not relevant for our implementation */
	if (PMIX_CHECK_KEY(this, PMIX_GET_REFRESH_CACHE)) {
	    /* we do not manage an own modex cache */
	    continue;
	}

	/* info with required directive are not allowed to be ignored */
	if (PMIX_INFO_IS_REQUIRED(this)) {
	    char *iStr = PMIx_Info_string(this);
	    flog("ERROR required info[%zd] '%s'\n", i, iStr);
	    free(iStr);
	    strvDestroy(reqKeys);
	    return PMIX_ERR_NOT_SUPPORTED;
	}

	/* inform about lacking implementation */
	flog("ignoring unimplemented [key '%s' flags '%s' value.type '%s']\n",
	     info[i].key, PMIx_Info_directives_string(info[i].flags),
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
	flog("pspmix_service_sendModexDataRequest(%s) failed\n",
	     pmixProcStr(proc));
	PMIX_PROC_DESTRUCT(&mdata->proc);
	strvDestroy(reqKeys);
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

    fdbg(PSPMIX_LOG_CALL, "ndata %zu proc %s\n", ndata, pmixProcStr(&mdata->proc));

    mdata->data = data;
    mdata->ndata = ndata;

    if (status != PMIX_SUCCESS) {
	flog("modex data request for %s failed: %s\n",
	     pmixProcStr(&mdata->proc), PMIx_Error_string(status));
    }

    pspmix_service_sendModexDataResponse(status, mdata);
}

/**
 * @brief Check for availability of all required keys at a client
 *
 * @param proc     client process
 * @param reqKeys  array of keys (NULL terminated)
 */
static bool checkKeyAvailability(pmix_proc_t *proc, strv_t reqKeys)
{
    if (!strvSize(reqKeys)) return true;

    void *infolist = PMIx_Info_list_start();

    bool flag = true;
    INFO_LIST_ADD(infolist, PMIX_IMMEDIATE, &flag, PMIX_BOOL);
    INFO_LIST_ADD(infolist, PMIX_GET_POINTER_VALUES, &flag, PMIX_BOOL);

    pmix_data_array_t info = PMIX_DATA_ARRAY_STATIC_INIT;
    INFO_LIST_CONVERT(infolist, &info);

    for (char **key = strvGetArray(reqKeys); key && *key; key++) {
	pmix_value_t *val;
	pmix_status_t status = PMIx_Get(proc, *key, info.array, info.size,
					&val);
	switch (status) {
	    case PMIX_SUCCESS:
		fdbg(PSPMIX_LOG_MODEX, "found '%s' for rank %d\n", *key,
		     proc->rank);
		break;
	    case PMIX_ERR_NOT_FOUND:
		fdbg(PSPMIX_LOG_MODEX, "not found '%s' for rank %d\n", *key,
		     proc->rank);
		PMIX_DATA_ARRAY_DESTRUCT(&info);
		return false;
	    case PMIX_ERR_BAD_PARAM:
	    case PMIX_ERR_EXISTS_OUTSIDE_SCOPE:
		flog("PMIx_get(proc %s key %s) failed: %s\n", pmixProcStr(proc),
		     *key, PMIx_Error_string(status));
		PMIX_DATA_ARRAY_DESTRUCT(&info);
		return false;
	}
    }
    PMIX_DATA_ARRAY_DESTRUCT(&info);
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
	flog("no mdata for %d\n", timerID);
	return;
    }

    if (checkKeyAvailability(&mdata->proc, mdata->reqKeys)) {
	Timer_remove(timerID);
	/* there are either no keys required or all available */
	pmix_status_t status =
		PMIx_server_dmodex_request(&mdata->proc, requestModexData_cb,
					   mdata);
	if (status != PMIX_SUCCESS) {
	    flog("PMIx_server_dmodex_request() failed: %s\n",
		 PMIx_Error_string(status));
	}
	return;
    }

    fdbg(PSPMIX_LOG_MODEX, "not yet found all required keys for rank %d\n",
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
    fdbg(PSPMIX_LOG_CALL, "proc %s\n", pmixProcStr(&mdata->proc));

    /* store time processing of the request has started */
    mdata->reqtime = time(NULL);

    if (checkKeyAvailability(&mdata->proc, mdata->reqKeys)) {
	/* there are either no keys required or all available */
	pmix_status_t status =
		PMIx_server_dmodex_request(&mdata->proc, requestModexData_cb,
					   mdata);
	if (status != PMIX_SUCCESS) {
	    flog("PMIx_server_dmodex_request() failed: %s\n",
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    flog("NOT IMPLEMENTED\n");

    // @todo implement
#if 0
    size_t errcount = 0;
    for (size_t i = 0; i < ninfo; i++) {
	const char *encval = encodeValue(&(info[i].value), proc->rank);
	bool ret = pspmix_service_putToKVS(proc->nspace, info[i].key, encval);
	if (!ret) errcount++;

	/* inform about lacking implementation */
	flog("ignoring flags for key '%s' flags '%s' (not implemented)\n",
	     info[i].key, PMIx_Info_directives_string(info[i].flags));
    }

    if (cbfunc) cbfunc(errcount == 0 ? PMIX_SUCCESS : PMIX_ERROR, cbdata);

    return PMIX_SUCCESS;
#endif

    return __PSPMIX_NOT_IMPLEMENTED;
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    flog("NOT IMPLEMENTED\n");

    /* @todo implement */
#if 0
    const char *encval;

    /* inform about lacking implementation */
    for (size_t i = 0; i < ninfo; i++) {
	flog("ignoring info [key '%s' flags '%s' value.type '%s']"
	     " (not implemented)\n", info[i].key,
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
	    flog("error getting key '%s' in namespace '%s'\n", keys[i],
		 proc->nspace);
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

    return __PSPMIX_NOT_IMPLEMENTED;
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    flog("NOT IMPLEMENTED\n");

    // @todo implement

    return __PSPMIX_NOT_IMPLEMENTED;
}

/* tell server helper library about spawn finished
 */
void pspmix_server_spawnRes(bool success, spawndata_t *sdata,
			    const char *nspace)
{
    if (!sdata || !sdata->cbfunc) {
	flog("success %s sdata %p: missing callback\n",
	     success ? "true" : "false", sdata);
	return;
    }

    fdbg(PSPMIX_LOG_CALL, "success %s\n", success ? "true" : "false");

    pmix_status_t status = success ? PMIX_SUCCESS : PMIX_ERROR;

    pmix_nspace_t ns;
    PMIX_LOAD_NSPACE(ns, nspace ? nspace : "none");
    sdata->cbfunc(status, ns, sdata->cbdata);
}


typedef struct {
    char *wdir;
    char *prefix;
    char *host;
    char *hostfile;
    bool initrequired;
    char *nodetypes;
    char *mpiexecopts;
    char *srunopts;
    char *srunconstraint;
} SpawnInfo_t;

static SpawnInfo_t getSpawnInfo(const pmix_info_t info[], size_t ninfo)
{
    SpawnInfo_t si = { NULL, NULL, NULL, NULL, false, NULL, NULL, NULL, NULL };

    /* handle command directives */
    /* Host environments are required to support the following attributes when
       present in either the job_info or the info array of an element of the
       apps array:
       PMIX_WDIR "pmix.wdir" (char* )
	 Working directory for spawned processes.
       PMIX_SET_SESSION_CWD "pmix.ssncwd" (bool)
	 Set the current working directory to the session working directory
	 assigned by the RM - can be assigned to the entire job (by including
	 attribute in the job_info array) or on a per-application basis in
	 the info array for each pmix_app_t.
       PMIX_PREFIX "pmix.prefix" (char* )
	 Prefix to use for starting spawned processes
	 i.e., the directory where the executable can be found.
       PMIX_HOST "pmix.host" (char* )
	 Comma-delimited list of hosts to use for spawned processes.
       PMIX_HOSTFILE "pmix.hostfile" (char* )
	 Hostfile to use for spawned processes.
     */
    for (size_t i = 0; i < ninfo; i++) {
	const pmix_info_t *this = info + i;

	char *iStr = PMIx_Info_string(this);
	fdbg(PSPMIX_LOG_SPAWN, "info[%zd] '%s'\n", i, iStr);
	free(iStr);

	if (PMIX_CHECK_KEY(this, PMIX_WDIR)) {
	    si.wdir = this->value.data.string;
	    continue;
	}

	if (PMIX_CHECK_KEY(this, PMIX_SET_SESSION_CWD)) {
	    /* @todo What is the session cwd? */
	    continue;
	}

	/*
	 * Starting from version 5.0.2 OpenPMIx does already apply the prefix
	 * and then never pass it again to us.
	 *
	 * Clarification to the standard is ongoing:
	 * https://github.com/pmix/pmix-standard/issues/506
	 */
	if (PMIX_CHECK_KEY(this, PMIX_PREFIX)) {
	    si.prefix = this->value.data.string;
	    continue;
	}

	if (PMIX_CHECK_KEY(this, PMIX_HOST)) {
	    si.host = this->value.data.string;
	    continue;
	}

	if (PMIX_CHECK_KEY(this, PMIX_HOSTFILE)) {
	    si.hostfile = this->value.data.string;
	    continue;
	}

	if (PMIX_CHECK_KEY(this, "pspmix.nodetypes")) {
	    si.nodetypes = this->value.data.string;
	    continue;
	}

	if (PMIX_CHECK_KEY(this, "pspmix.mpiexecopts")) {
	    si.mpiexecopts = this->value.data.string;
	    continue;
	}

	if (PMIX_CHECK_KEY(this, "pspmix.srunopts")) {
	    si.srunopts = this->value.data.string;
	    continue;
	}

	if (PMIX_CHECK_KEY(this, "pspmix.srunconstraint")) {
	    si.srunconstraint = this->value.data.string;
	    continue;
	}
	/*
	 * @todo
	 *
	 * find a way to add custom info fields to add
	 * - initrequired:  a spawn is not successful before all clients have
	 *                  called PMIx_Init(). Without that option set, it
	 *                  is reported as successful once all clients are
	 *                  spawned (called exec()).
	 */

	/* inform about lacking implementation */
	iStr = PMIx_Info_string(this);
	flog("ignoring unimplemented info[%zd] '%s'\n", i, iStr);
	free(iStr);
    }

    return si;
}

/**
 * @brief Spawn a set of applications/processes as per the PMIx_Spawn API.
 *
 * Note that applications are not required to be MPI or any other programming
 * model. Thus, the host server cannot make any assumptions as to their required
 * support. The callback function is to be executed once all processes have been
 * started. An error in starting any application or process in this request
 * shall cause all applications and processes in the request to be terminated,
 * and an error returned to the originating caller.
 *
 * Note that a timeout can be specified in the job_info array to indicate that
 * failure to start the requested job within the given time should result in
 * termination to avoid hangs.
 *
 * @param proc      pmix_proc_t structure of the process making the request
 * @param info      Array of info structures for the whole job
 * @param ninfo     Number of elements in the job_info array
 * @param apps      Array of pmix_app_t structure
 * @param napps     Number of elements in the apps array
 * @param cbfunc    Callback function
 * @param cbdata    Data to be passed to the callback function
 */
/* pmix_server_spawn_fn_t  */
static pmix_status_t server_spawn_cb(const pmix_proc_t *proc,
				     const pmix_info_t job_info[], size_t ninfo,
				     const pmix_app_t apps[], size_t napps,
				     pmix_spawn_cbfunc_t cbfunc, void *cbdata)
{

    /* assert input from pmix lib is as we expect it */
    if (!proc || (ninfo && !job_info) || !napps || !apps)
	return PMIX_ERR_BAD_PARAM;
    for (size_t a = 0; a < napps; a++) {
	if (!apps[a].cmd || !apps[a].argv) return PMIX_ERR_BAD_PARAM;
    }

    if (mset(PSPMIX_LOG_CALL)) {
	flog("proc %s\n", pmixProcStr(proc));

	for (size_t i = 0; i < napps; i++) {
	    flog("cmd='%s' argv='", apps[i].cmd);
	    for (char **tmp = apps[i].argv; *tmp; tmp++) mlog("%s ", *tmp);
	    if (!apps[i].env) {
		mlog("' env=NULL");
	    } else {
		mlog("' env='");
		for (char **tmp = apps[i].env; *tmp; tmp++) mlog("%s ", *tmp);
		mlog("'");
	    }
	    mlog(" cwd='%s' maxprocs=%d ninfo=%zd\n", apps[i].cwd,
		 apps[i].maxprocs, apps[i].ninfo);
	}
    }

    SpawnInfo_t si_job = getSpawnInfo(job_info, ninfo);

    /* Restructure information
     * Note that we need to copy everything that we want to access for
     * asynchonous processing after this function returned, since the PMIx
     * standard (v4) does not provide any guaranties about the availability
     * of any data passed to this function after it returned.
     * Would be great to have this data available until the cbfunc is called
     * unless PMIX_OPERATION_SUCCEEDED is returned to avoid data duplication.
     * To avoid copying everything, we invalidate all data that are only needed
     * to setup the spawn but not for later processing. */
    PspmixSpawnApp_t *sapps = umalloc(napps * sizeof(*sapps));

    for (size_t a = 0; a < napps; a++) {

	SpawnInfo_t si_app = getSpawnInfo(apps[a].info, apps[a].ninfo);

	/* apps[x].cmd is the command to be executed.
	 * We ignore and override apps[x].argv[0] since it is set again by each
	 * process forwarder before the execve() call.
	 *
	 * see https://github.com/openpmix/openpmix/issues/3321
	 */
	strv_t argv = strvConstruct(apps[a].argv);
	sapps[a].argv = strvStealArray(argv);
	sapps[a].argv[0] = apps[a].cmd;

	/*
	 * Starting from version 5.0.2 OpenPMIx does already apply the prefix
	 * and then never pass it again to us, so sapps[a].prefix will be NULL.
	 */
	sapps[a].prefix = si_app.prefix ? si_app.prefix : si_job.prefix;

	if (sapps[a].prefix) {
	    /* add prefix */
	    sapps[a].argv[0] = PSC_concat(sapps[a].prefix, "/", apps[a].cmd);

	    flog("prefix '%s' applied to app %zu command: '%s'\n",
		 sapps[a].prefix, a, sapps[a].argv[0]);
	}

	sapps[a].maxprocs = apps[a].maxprocs;
	sapps[a].env = apps[a].env;


	sapps[a].wdir = si_app.wdir ? si_app.wdir :
			si_job.wdir ? si_job.wdir : apps[a].cwd;
	sapps[a].host = si_app.host ? si_app.host : si_job.host;
	sapps[a].hostfile = si_app.hostfile ? si_app.hostfile :
				 si_job.hostfile;
	sapps[a].nodetypes = si_app.nodetypes;
	sapps[a].mpiexecopts = si_app.mpiexecopts;
	sapps[a].srunopts = si_app.srunopts;
	sapps[a].srunconstraint = si_app.srunconstraint;
    }

    /* handle additional options */
    uint32_t opts = 0;
    if (si_job.initrequired) opts |= PSPMIX_SPAWNOPT_INITREQUIRED;

    /* initialize return data struct */
    spawndata_t *sdata = ucalloc(sizeof(*sdata));
    sdata->cbfunc = cbfunc;
    sdata->cbdata = cbdata;

    PspmixSpawnHints_t hints = {
	.nodetypes = si_job.nodetypes,
	.mpiexecopts = si_job.mpiexecopts,
	.srunopts = si_job.srunopts
    };

    bool ret = pspmix_service_spawn(proc, napps, sapps, sdata, opts, &hints);


    for (size_t a = 0; a < napps; a++) {

	if (sapps[a].prefix) {
	    ufree(sapps[a].argv[0]); /* allocated by PSC_concat() */
	}
	ufree(sapps[a].argv); /* allocated by strvConstruct() */

	/* Invalidate all data not copied */
	sapps[a].prefix = NULL;
	sapps[a].argv = NULL;
	sapps[a].env = NULL;
	sapps[a].wdir = NULL;
	sapps[a].host = NULL;
	sapps[a].hostfile = NULL;
	sapps[a].nodetypes = NULL;
	sapps[a].mpiexecopts = NULL;
	sapps[a].srunopts = NULL;
	sapps[a].srunconstraint = NULL;
    }

    if (!ret) {
	ufree(sapps);
	ufree(sdata);
    }

    return ret ? PMIX_SUCCESS : PMIX_ERROR;

    /* By default, the spawned processes will be PMIx “connected” to the parent
       process upon successful launch (see Section 12.3 for details). This
       includes that (a) the parent process will be given a copy of the new
       job’s information so it can query job-level info without incurring any
       communication penalties, (b) newly spawned child processes will receive
       a copy of the parent processes job-level info, and (c) both the parent
       process and members of the child job will receive notification of errors
       from processes in their combined assemblage.

       The PMIx definition of connected solely implies that the host environment
       should treat the failure of any process in the assemblage as a reportable
       event, taking action on the assemblage as if it were a single application.
       For example, if the environment defaults (in the absence of any application
       directives) to terminating an application upon failure of any process in
       that application, then the environment should terminate all processes in
       the connected assemblage upon failure of any member.

       The host environment may choose to assign a new namespace to the connected
       assemblage and/or assign new ranks for its members for its own internal
       tracking purposes. However, it is not required to communicate such
       assignments to the participants (e.g., in response to an appropriate call
       to PMIx_Query_info_nb). The host environment is required to generate a
       PMIX_ERR_PROC_TERM_WO_SYNC event should any process in the assemblage
       terminate or call PMIx_Finalize without first disconnecting from the
       assemblage. If the job including the process is terminated as a result of
       that action, then the host environment is required to also generate the
       PMIX_ERR_JOB_TERM_WO_SYNC for all jobs that were terminated as a result.

       Advice to PMIx server hosts
       The connect operation does not require the exchange of job-level
       information nor the inclusion of information posted by participating
       processes via PMIx_Put. Indeed, the callback function utilized in
       pmix_server_connect_fn_t cannot pass information back into the PMIx
       server library. However, host environments are advised that collecting
       such information at the participating daemons represents an optimization
       opportunity as participating processes are likely to request such
       information after the connect operation completes.
     */

    /* Behavior of individual resource managers may differ, but it is expected
       that failure of any application process to start will result in
       termination/cleanup of all processes in the newly spawned job and return
       of an error code to the caller.
     */

    /* In addition to the generic error constants, the following spawn-specific
       error constants may be returned by the spawn APIs:
       PMIX_ERR_JOB_ALLOC_FAILED
         The job request could not be executed due to failure to obtain the
         specified allocation
       PMIX_ERR_JOB_APP_NOT_EXECUTABLE
         The specified application executable either could not be found, or
         lacks execution privileges.
       PMIX_ERR_JOB_NO_EXE_SPECIFIED
         The job request did not specify an executable.
       PMIX_ERR_JOB_FAILED_TO_MAP
         The launcher was unable to map the processes for the specified job
         request.
       PMIX_ERR_JOB_FAILED_TO_LAUNCH
         One or more processes in the job request failed to launch
       PMIX_ERR_JOB_EXE_NOT_FOUND (Provisional)
         Specified executable not found
       PMIX_ERR_JOB_INSUFFICIENT_RESOURCES (Provisional)
       PMIX_ERR_JOB_SYS_OP_FAILED (Provisional)
       PMIX_ERR_JOB_WDIR_NOT_FOUND (Provisional)
     */
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    flog("NOT IMPLEMENTED\n");

    // @todo implement

    /* not implemented yet */
    return __PSPMIX_NOT_IMPLEMENTED;
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    flog("NOT IMPLEMENTED\n");

    // @todo implement

    /* not implemented yet */
    return __PSPMIX_NOT_IMPLEMENTED;
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    flog("NOT IMPLEMENTED\n");

    // @todo implement

    /* not implemented yet */
    return __PSPMIX_NOT_IMPLEMENTED;
}

/* Deregister to receive notifications for the specified events to which the
 * PMIx server has previously registered. */
/* pmix_server_deregister_events_fn_t */
static pmix_status_t server_deregister_events_cb(pmix_status_t *codes, size_t ncodes,
						 pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    flog("NOT IMPLEMENTED\n");

    // @todo implement

    /* not implemented yet */
    return __PSPMIX_NOT_IMPLEMENTED;
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    flog("NOT IMPLEMENTED\n");

    // @todo implement

    /* not implemented */
    return __PSPMIX_NOT_IMPLEMENTED;
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    /* not implemented */
    return __PSPMIX_NOT_IMPLEMENTED;
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
    fdbg(PSPMIX_LOG_CALL, "proc %s\n", pmixProcStr(proc));

    // @todo implement

    for (size_t i = 0; i < nqueries; i++) {
	flog("query %zu:", i);
	for (size_t j = 0; queries[i].keys[j] != NULL; j++) {
	    mlog(" key '%s'", queries[i].keys[j]);
	}
	mlog(" (NOT IMPLEMENTED)\n");
    }

    /* not implemented */
    return __PSPMIX_NOT_IMPLEMENTED;
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    // @todo implement at low priority

    flog("NOT IMPLEMENTED\n");

    /* not implemented */
}

static void printInfoArray(char *arr_name, const pmix_info_t *arr,
			   size_t arr_size)
{
    flog("%s:\n", arr_name);
    for (size_t i = 0; i < arr_size; i++) {
	char * istr = PMIx_Info_string(arr+i);
	flog("  %s\n", istr);
	free(istr);
    }
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
    fdbg(PSPMIX_LOG_CALL, "client %s ndata %zd ndirs %zd\n", pmixProcStr(client),
	 ndata, ndirs);
    if (mset(PSPMIX_LOG_LOGGING)) {
	printInfoArray("data", data, ndata);
	printInfoArray("directives", directives, ndirs);
    }

    PspmixLogCall_t call = pspmix_service_newLogCall(client);

    for (size_t i = 0; i < ndirs; i++) {
	const pmix_info_t *this = directives + i;
	if (PMIX_CHECK_KEY(this, PMIX_LOG_ONCE)) {
	    if (PMIX_INFO_TRUE(this)) pspmix_service_setLogOnce(call);
	} else if (PMIX_CHECK_KEY(this, PMIX_LOG_SYSLOG_PRI)) {
	    pspmix_service_setSyslogPrio(call, this->value.data.integer);
	} else if (PMIX_CHECK_KEY(this, PMIX_LOG_TIMESTAMP)) {
	    pspmix_service_setTimeStamp(call, this->value.data.time);
	} else if (PMIX_CHECK_KEY(this, PMIX_LOG_SOURCE)) {
	    // ignore silently
	} else if (PMIX_CHECK_KEY(this, PMIX_LOG_GENERATE_TIMESTAMP)) {
	    // OpenPMIx has generated the PMIX_LOG_TIMESTAMP => ignore silently
	} else {
	    flog("ignoring unsupported directive '%s'\n", this->key);
	}
    }

    for (size_t i = 0; i < ndata; i++) {
	const pmix_info_t *this = data + i;
	if (PMIX_CHECK_KEY(this, PMIX_LOG_ONCE)) {
	    pspmix_service_setLogOnce(call);
	} else if (PMIX_CHECK_KEY(this, PMIX_LOG_STDERR)) {
	    pspmix_service_addLogRequest(call, PSPMIX_LC_STDERR,
					 this->value.data.string);
	} else if (PMIX_CHECK_KEY(this, PMIX_LOG_STDOUT)) {
	    pspmix_service_addLogRequest(call, PSPMIX_LC_STDOUT,
					 this->value.data.string);
	} else if (PMIX_CHECK_KEY(this, PMIX_LOG_SYSLOG)
		   || PMIX_CHECK_KEY(this, PMIX_LOG_LOCAL_SYSLOG)
		   || PMIX_CHECK_KEY(this, PMIX_LOG_GLOBAL_SYSLOG)) {
	    /* since all pspmix host servers act as gateway servers,
	     * there is no difference between LOCAL_SYSLOG and
	     * GLOBAL_SYSLOG. SYSLOG itself is a proxy to try
	     * GLOBAL_SYSLOG and LOCAL_SYSLOG in this order */
	    pspmix_service_addLogRequest(call, PSPMIX_LC_SYSLOG,
					 this->value.data.string);
	} else if (PMIX_CHECK_KEY(this, PMIX_LOG_EMAIL)
		   || PMIX_CHECK_KEY(this, PMIX_LOG_GLOBAL_DATASTORE)
		   || PMIX_CHECK_KEY(this, PMIX_LOG_JOB_RECORD)) {
	    pspmix_service_addLogRequest(call, PSPMIX_LC_UNSUPPORTED, NULL);
	} else {
	    flog("ignoring unknown or unsupported key '%s'\n", this->key);
	}
    }

    mycbfunc_t *cb = NULL;
    if (cbfunc) INIT_CBFUNC(cb, cbfunc, cbdata);

    pspmix_service_log(call, cb);
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
 *   disjoint from (i.e., not affiliated with) the allocation of the requester
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    flog("NOT IMPLEMENTED\n");

    // @todo implement at low priority

    return __PSPMIX_NOT_IMPLEMENTED;
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    // @todo implement at low priority

    flog("NOT IMPLEMENTED\n");

    /* not implemented */
    return __PSPMIX_NOT_IMPLEMENTED;
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    // @todo implement at low priority

    flog("NOT IMPLEMENTED\n");

    /* not implemented */
    return __PSPMIX_NOT_IMPLEMENTED;
}

/* Request a credential from the host environment. */
/* pmix_server_get_cred_fn_t */
static pmix_status_t server_get_cred_cb(const pmix_proc_t *proc,
					const pmix_info_t directives[],
					size_t ndirs,
					pmix_credential_cbfunc_t cbfunc,
					void *cbdata)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    // @todo implement at low priority

    flog("NOT IMPLEMENTED\n");

    /* not implemented */
    return __PSPMIX_NOT_IMPLEMENTED;
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    // @todo implement at low priority

    flog("NOT IMPLEMENTED\n");

    /* not implemented */
    return __PSPMIX_NOT_IMPLEMENTED;
}

/* Request the specified IO channels be forwarded from the given array of
 * processes */
/* pmix_server_iof_fn_t */
static pmix_status_t server_iof_cb(const pmix_proc_t procs[], size_t nprocs,
				   const pmix_info_t directives[], size_t ndirs,
				   pmix_iof_channel_t channels,
				   pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    // @todo implement at low priority

    flog("NOT IMPLEMENTED\n");

    /* not implemented */
    return __PSPMIX_NOT_IMPLEMENTED;
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    // @todo implement at low priority

    flog("NOT IMPLEMENTED\n");

    /* not implemented */
    return __PSPMIX_NOT_IMPLEMENTED;
}

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
    fdbg(PSPMIX_LOG_CALL, "\n");

    switch(op) {
	case PMIX_GROUP_CONSTRUCT:
	    flog("op: %s", "PMIX_GROUP_CONSTRUCT");
	    break;
	case PMIX_GROUP_DESTRUCT:
	    flog("op: %s", "PMIX_GROUP_DESTRUCT");
	    break;
	default:
	    flog("op: %d", op);
    }
    mlog(" grp: %s\n", grp);

    flog("procs:");
    for (size_t i = 0; i < nprocs; i++) {
	mlog(" %s", pmixProcStr(&procs[i]));
    }
    mlog("\n");

    flog("directives:");
    for (size_t i = 0; i < ndirs; i++) {
	char * istr = PMIx_Info_string(&directives[i]);
	mlog(" %s", istr);
	free(istr);
    }
    mlog("\n");

    // @todo implement at high priority

    flog("NOT IMPLEMENTED\n");

    /* not implemented */
    return __PSPMIX_NOT_IMPLEMENTED;
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    // @todo implement at low priority

    flog("NOT IMPLEMENTED\n");

    /* not implemented */
    return __PSPMIX_NOT_IMPLEMENTED;
}

/* struct holding the server callback functions */
static pmix_server_module_t module = {
    /* v1x interfaces */
    .client_connected = NULL, /* deprecated */
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
    .group = server_grp_cb,
    .fabric = server_fabric_cb,
    .client_connected2 = server_client_connected2_cb,
};

/* XXX */
static void errhandler(size_t evhdlr_registration_id, pmix_status_t status,
		       const pmix_proc_t *source,
		       pmix_info_t info[], size_t ninfo,
		       pmix_info_t results[], size_t nresults,
		       pmix_event_notification_cbfunc_fn_t cbfunc, void *cbdata)
{
    fdbg(PSPMIX_LOG_CALL, "status %d proc %s ninfo %lu nresults %lu\n",
	 status, pmixProcStr(source), ninfo, nresults);
}

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
    const char *hostname = PSIDnodes_getHostname(PSC_getMyID());
    if (!hostname) {
	fdbg(PSPMIX_LOG_VERBOSE, "unable to get my own hostname from psid"
	     " => falling back to resolver\n");
	hostname = getHostnameByNodeId(PSC_getMyID());
    }
    if (!hostname) {
	flog("unable to get my own hostname\n");
	PMIx_Info_list_release(list);
	return false;
    }
    INFO_LIST_ADD(list, PMIX_SERVER_HOSTNAME, hostname, PMIX_STRING);

    pmix_status_t status;
    status = PMIx_Info_list_convert(list, sessionInfo);
    PMIx_Info_list_release(list);
    if (status != PMIX_SUCCESS) {
	flog("converting info list to array failed: %s\n",
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    mycbdata_t *data = cbdata;

    data->status = status;

    SET_CBDATA_AVAIL(data);
}

/**
 * To be called by error handler registration function to provide success state
 */
static void registerErrorHandler_cb (pmix_status_t status,
				     size_t errhandler_ref, void *cbdata)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    mycbdata_t *data = cbdata;

    data->status = status;

    SET_CBDATA_AVAIL(data);
}

bool pspmix_server_init(char *nspace, pmix_rank_t rank, const char *clusterid,
			const char *srvtmpdir, const char *systmpdir)
{
    fdbg(PSPMIX_LOG_CALL, "nspace %s rank %d srvtmpdir %s systmpdir %s\n",
	 nspace, rank, srvtmpdir, systmpdir);

    /* print some interesting environment variables if set */
    for (size_t i = 0; environ[i]; i++) {
	if (!strncmp(environ[i], "PMIX_DEBUG", 10)
	    || !strncmp (environ[i], "PMIX_OUTPUT", 11)
	    || !strncmp (environ[i], "PMIX_MCA_", 9)) {
	    flog("%s set\n", environ[i]);
	}
    }

    void *list = PMIx_Info_list_start();

    /* Name of the namespace to use for this PMIx server */
    INFO_LIST_ADD(list, PMIX_SERVER_NSPACE, nspace, PMIX_STRING);

    /* Rank of this PMIx server */
    INFO_LIST_ADD(list, PMIX_SERVER_RANK, &rank, PMIX_PROC_RANK);

    /* Top-level temporary directory for all client processes connected to this
     * server, and where the PMIx server will place its tool rendezvous point
     * and contact information. */
    if (srvtmpdir) {
	INFO_LIST_ADD(list, PMIX_SERVER_TMPDIR, srvtmpdir, PMIX_STRING);
    }

    /* Temporary directory for this system, and where a PMIx server that
     * declares itself to be a system-level server will place a tool rendezvous
     * point and contact information.
     * Needs to be persistent and visible across sessions
     * see https://github.com/openpmix/openpmix/issues/3349 */
    if (systmpdir) {
	INFO_LIST_ADD(list, PMIX_SYSTEM_TMPDIR, systmpdir, PMIX_STRING);
    }

    /* The host RM wants to declare itself as willing to accept tool connection
     * requests. */
    bool tmpbool = false;
    INFO_LIST_ADD(list, PMIX_SERVER_TOOL_SUPPORT, &tmpbool, PMIX_BOOL);

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
    INFO_LIST_ADD(list, PMIX_SERVER_SYSTEM_SUPPORT, &tmpbool, PMIX_BOOL);

    /* The host RM wants to declare itself as being the local session server for
     * PMIx connection requests. */
    tmpbool = false;
    INFO_LIST_ADD(list, PMIX_SERVER_SESSION_SUPPORT, &tmpbool, PMIX_BOOL);

    /* Server is acting as a gateway for PMIx requests that cannot be serviced
     * on backend nodes (e.g., logging to email, recording syslogs). */
    tmpbool = true;
    INFO_LIST_ADD(list, PMIX_SERVER_GATEWAY, &tmpbool, PMIX_BOOL);

    /* Server is supporting system scheduler and desires access to appropriate
     * WLM-supporting features. Indicates that the library is to be initialized
     * for scheduler support. */
    tmpbool = false;
    INFO_LIST_ADD(list, PMIX_SERVER_SCHEDULER, &tmpbool, PMIX_BOOL);

# if 0 /* optional attributes */

    /* Disable legacy UNIX socket (usock) support. */
    tmpbool = false;
    INFO_LIST_ADD(list, PMIX_USOCK_DISABLE, &tmpbool, PMIX_BOOL);

    /* POSIX mode_t (9 bits valid). */
    uint32_t tmpuint32 = 0;
    INFO_LIST_ADD(list, PMIX_SOCKET_MODE, &tmpuint32, PMIX_UINT32);

    /* Use only one rendezvous socket, letting priorities and/or environment
     * parameters select the active transport. */
    tmpbool = false;
    INFO_LIST_ADD(list, PMIX_SINGLE_LISTENER, &tmpbool, PMIX_BOOL);

    /* If provided, directs that the TCP URI be reported and indicates the
     * desired method of reporting: ’-’ for stdout, ’+’ for stderr, or filename.
     * If the library supports TCP socket connections, this attribute may be
     * supported for reporting the URI. */
    tmpstr = "";
    INFO_LIST_ADD(list, PMIX_TCP_REPORT_URI, &tmpstr, PMIX_STRING);

    /* Comma-delimited list of devices and/or CIDR notation to include when
     * establishing the TCP connection. If the library supports TCP socket
     * connections, this attribute may be supported for specifying the
     * interfaces to be used. */
    tmpstr = "";
    INFO_LIST_ADD(list, PMIX_TCP_IF_INCLUDE, &tmpstr, PMIX_STRING);

    /* Comma-delimited list of devices and/or CIDR notation to exclude when
     * establishing the TCP connection. If the library supports TCP socket
     * connections, this attribute may be supported for specifying the
     * interfaces that are not to be used. */
    tmpstr = "";
    INFO_LIST_ADD(list, PMIX_TCP_IF_EXCLUDE, &tmpstr, PMIX_STRING);

    /* The IPv4 port to be used.. If the library supports IPV4 connections, this
     * attribute may be supported for specifying the port to be used. */
    int tmpint = 1234;
    INFO_LIST_ADD(list, PMIX_TCP_IPV4_PORT, &tmpint, PMIX_INT);

    /* The IPv6 port to be used. If the library supports IPV6 connections, this
     * attribute may be supported for specifying the port to be used. */
    int tmpint = 1234;
    INFO_LIST_ADD(list, PMIX_TCP_IPV6_PORT, &tmpint, PMIX_INT);

    /* Set to true to disable IPv4 family of addresses. If the library supports
     * IPV4 connections, this attribute may be supported for disabling it. */
    tmpbool = false;
    INFO_LIST_ADD(list, PMIX_TCP_DISABLE_IPV4, &tmpbool, PMIX_BOOL);

    /* Set to true to disable IPv6 family of addresses. If the library supports
     * IPV6 connections, this attribute may be supported for disabling it. */
    tmpbool = false;
    INFO_LIST_ADD(list, PMIX_TCP_DISABLE_IPV6, &tmpbool, PMIX_BOOL);

    /* Allow connections from remote tools. Forces the PMIx server to not
     * exclusively use loopback device. */
    tmpbool = false;
    INFO_LIST_ADD(list, PMIX_SERVER_REMOTE_CONNECTIONS, &tmpbool, PMIX_BOOL);

    /* The host shall progress the PMIx library via calls to PMIx_Progress */
    tmpbool = false;
    INFO_LIST_ADD(list, PMIX_EXTERNAL_PROGRESS, &tmpbool, PMIX_BOOL);

    /* Pointer to an event_base to use in place of the internal progress thread.
     * All PMIx library events are to be assigned to the provided event base.
     * The event base must be compatible with the event library used by the PMIx
     * implementation - e.g., either both the host and PMIx library must use
     * libevent, or both must use libev. Cross-matches are unlikely to work and
     * should be avoided - it is the responsibility of the host to ensure that
     * the PMIx implementation supports (and was built with) the appropriate
     * event library. */
    void *tmpptr = NULL;
    INFO_LIST_ADD(list, PMIX_EVENT_BASE, &tmpptr, PMIX_POINTER);

    /* Provide a pointer to an implementation-specific description of the local
     * node topology. */
    pmix_topology_t tmptopo;
    PMIX_TOPOLOGY_CONSTRUCT(&tmptopo);
    tmptopo.source = "custom";
    tmptopo.topology = NULL;
    INFO_LIST_ADD(list, PMIX_TOPOLOGY2, &tmptopo, PMIX_TOPO);

    /* The PMIx server is to share its copy of the local node topology (whether
     * given to it or self-discovered) with any clients. */
    tmpbool = false;
    INFO_LIST_ADD(list, PMIX_SERVER_SHARE_TOPOLOGY, &tmpbool, PMIX_BOOL);

    /* Enable PMIx internal monitoring by the PMIx server. */
    tmpbool = false;
    INFO_LIST_ADD(list, PMIX_SERVER_ENABLE_MONITORING, &tmpbool, PMIX_BOOL);

    /* The nodes comprising the session are homogeneous - i.e., they each
     * contain the same number of identical packages, fabric interfaces, GPUs,
     * and other devices. */
    tmpbool = false;
    INFO_LIST_ADD(list, PMIX_HOMOGENEOUS_SYSTEM, &tmpbool, PMIX_BOOL);

    /* Time when the server started - i.e., when the server created it’s
     * rendezvous file (given in ctime string format). */
    time_t tmptime = time(NULL);
    char *tmpstr = ctime(&tmptime);
    INFO_LIST_ADD(list, PMIX_SERVER_START_TIME, tmpstr, PMIX_STRING);
# endif /* optional attributes */

    fdbg(PSPMIX_LOG_VERBOSE, "setting nspace %s rank %d\n", nspace, rank);

    pmix_data_array_t info = PMIX_DATA_ARRAY_STATIC_INIT;
    INFO_LIST_CONVERT(list, &info);

    if (mset(PSPMIX_LOG_INFOARR)) {
	flog("PMIx_server_init info:\n");
	for (size_t j = 0; j < info.size; j++) {
	    char * istr = PMIx_Info_string(&((pmix_info_t *)info.array)[j]);
	    mlog("   %s\n", istr);
	    free(istr);
	}
    }

    /* initialize server library */
    pmix_status_t status = PMIx_server_init(&module, info.array, info.size);
    PMIX_DATA_ARRAY_DESTRUCT(&info);
    if (status != PMIX_SUCCESS) {
	flog("PMIx_server_init() failed: %s\n", PMIx_Error_string(status));
	return false;
    }
    fdbg(PSPMIX_LOG_VERBOSE, "PMIx_server_init() successful\n");

    /* tell the server common information */

    pmix_data_array_t sessionInfo = PMIX_DATA_ARRAY_STATIC_INIT;
    if (!fillServerSessionArray(&sessionInfo, clusterid)) {
	flog("filling server session info failed\n");
	return false;
    }

    mycbdata_t cbdata;
    INIT_CBDATA(cbdata, 1);
    PMIX_INFO_LOAD(cbdata.info, PMIX_SESSION_INFO_ARRAY, &sessionInfo,
		   PMIX_DATA_ARRAY);

    if (mset(PSPMIX_LOG_INFOARR)) {
	flog("PMIx_server_register_resources info:\n");
	for (size_t j = 0; j < cbdata.ninfo; j++)
	    mlog("   %s\n", PMIx_Info_string(&cbdata.info[j]));
    }

    status = PMIx_server_register_resources(cbdata.info, cbdata.ninfo,
					    registerResources_cb, &cbdata);
    if (status != PMIX_SUCCESS) {
	flog("PMIx_server_register_resources() failed: %s\n",
	     PMIx_Error_string(status));
	DESTROY_CBDATA(cbdata);
	return false;
    }
    WAIT_FOR_CBDATA(cbdata);

    if (cbdata.status != PMIX_SUCCESS) {
	flog("callback from register resources failed: %s\n",
	     PMIx_Error_string(cbdata.status));
	DESTROY_CBDATA(cbdata);
	return false;
    }
    DESTROY_CBDATA(cbdata);

    fdbg(PSPMIX_LOG_VERBOSE, "PMIx_server_register_resources() successful\n");

    /* register the error handler */
    INIT_CBDATA(cbdata, 0);
    PMIx_Register_event_handler(NULL, 0, NULL, 0,
	    errhandler, registerErrorHandler_cb, &cbdata);
    WAIT_FOR_CBDATA(cbdata);

    if (cbdata.status != PMIX_SUCCESS) {
	flog("callback from register error handler failed: %s\n",
	     PMIx_Error_string(cbdata.status));
	DESTROY_CBDATA(cbdata);
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
			flog("passing %s\n", tmp);
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

    fdbg(PSPMIX_LOG_CALL, "\n");

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
    strbuf_t pmap = strbufNew(NULL);

    list_t *n;
    list_for_each(n, procMap) {
	PspmixNode_t *node = list_entry(n, PspmixNode_t, next);
	if (strbufLen(pmap)) strbufAdd(pmap, ";");
	for (size_t i = 0; i < node->procs.len; i++) {
	    PspmixProcess_t *proc = vectorGet(&node->procs, i, PspmixProcess_t);
	    if (i) strbufAdd(pmap, ",");
	    char buf[24];
	    sprintf(buf, "%u", proc->rank);
	    strbufAdd(pmap, buf);
	}
    }

    return strbufSteal(pmap);
}

/**
 * Get string representation of nodes ranks
 *
 * @param procMap      process map (which process runs on which node)
 * @param nodeID       ParaStation ID of this node
 * @param pmap   <OUT> vector to hold the process map string
 * @param lpeers <OUT> vector to hold the local peers string
 *
 * @return true on success, false else
 */
static char* getNodeRanksString(PspmixNode_t *node)
{
    strbuf_t ranks = strbufNew(NULL);

    for (size_t i = 0; i < node->procs.len; i++) {
	PspmixProcess_t *proc = vectorGet(&node->procs, i, PspmixProcess_t);
	if (i) strbufAdd(ranks, ",");
	char buf[24];
	sprintf(buf, "%u", proc->rank);
	strbufAdd(ranks, buf);
    }

    return strbufSteal(ranks);
}

static void fillSessionInfoArray(pmix_data_array_t *sessionInfo,
				 uint32_t session_id, uint32_t universe_size)
{
    void *list = PMIx_Info_list_start();

    /* session id
     * (needed to be the first entry
     *  @todo find out since which version this is no longer the case) */
    INFO_LIST_ADD(list, PMIX_SESSION_ID, &session_id, PMIX_UINT32);

    /* same as PMIX_MAX_PROCS below, here for historical reasons */
    INFO_LIST_ADD(list, PMIX_UNIV_SIZE, &universe_size, PMIX_UINT32);

    /* number of slots in this session */
    INFO_LIST_ADD(list, PMIX_MAX_PROCS, &universe_size, PMIX_UINT32);

    /* optional infos (PMIx v4.0):
     * * PMIX_ALLOCATED_NODELIST "pmix.alist" (char*)
     *     Comma-delimited list or regular expression of all nodes in the
     *     specified realm regardless of whether or not they currently host
     *     processes. Defaults to the job realm.
     */

    INFO_LIST_CONVERT(list, sessionInfo);
}

/**
 * @brief Get string with comma separated hostname list.
 *
 * @return Returns the requested string or "", needs to be freed
 */
static char * getNodelistString(list_t *procMap)
{
    strbuf_t buf = strbufNew(NULL);

    list_t *n;
    list_for_each(n, procMap) {
	PspmixNode_t *node = list_entry(n, PspmixNode_t, next);
	if (strbufLen(buf)) strbufAdd(buf, ",");
	strbufAdd(buf, node->hostname);
    }
    strbufAdd(buf, ""); // make sure to never return NULL

    return strbufSteal(buf);
}

static void fillJobInfoArray(pmix_data_array_t *jobInfo,
			     const char *nspace, const char *jobId,
			     uint32_t jobSize, uint32_t maxProcs,
			     list_t *procMap,  uint32_t numApps,
			     pmix_rank_t grankOffset)
{
    void *list = PMIx_Info_list_start();

    /* namespace identifier (set even if not needed for call to
     * PMIx_server_register_nspace() as explicitly stated in the standard */
    INFO_LIST_ADD(list, PMIX_NSPACE, nspace, PMIX_STRING);

    /* job identifier (as assigned by the scheduler) */
    INFO_LIST_ADD(list, PMIX_JOBID, jobId, PMIX_STRING);

    /* total num of processes in this job across all contained applications */
    INFO_LIST_ADD(list, PMIX_JOB_SIZE, &jobSize, PMIX_UINT32);

    /* Maximum number of processes in this job */
    INFO_LIST_ADD(list, PMIX_MAX_PROCS, &maxProcs, PMIX_UINT32);

    /* regex of nodes containing procs for this job */
    char *nodelist_s = getNodelistString(procMap);

    fdbg(PSPMIX_LOG_INFOARR, "nodelist string created: '%s'\n", nodelist_s);

    char *nodelist_r;
    PMIx_generate_regex(nodelist_s, &nodelist_r);
    ufree(nodelist_s);
    INFO_LIST_ADD(list, PMIX_NODE_MAP, nodelist_r, PMIX_STRING);
    ufree(nodelist_r);

    /* regex describing procs on each node within this job */
    char *pmap_s = getProcessMapString(procMap);

    fdbg(PSPMIX_LOG_INFOARR, "proc_map string created: '%s'\n", pmap_s);

    char *pmap_r;
    PMIx_generate_ppn(pmap_s, &pmap_r);
    ufree(pmap_s);
    INFO_LIST_ADD(list, PMIX_PROC_MAP, pmap_r, PMIX_STRING);
    ufree(pmap_r);

    /* number of applications in this job (required if > 1) */
    INFO_LIST_ADD(list, PMIX_JOB_NUM_APPS, &numApps, PMIX_UINT32);

    /* starting global rank of the job (probably needed for spawn support)
     * should be the same as PMIX_GLOBAL_RANK of rank 0 of this job */
    INFO_LIST_ADD(list, PMIX_NPROC_OFFSET, &grankOffset, PMIX_PROC_RANK);

    /* optional infos (PMIx v3.0, v4.1 and v5.0):
     * * PMIX_ALLOCATED_NODELIST "pmix.alist" (char*)
     *     Comma-delimited list of all nodes in this allocation regardless of
     *     whether or not they currently host processes
     *     (ambiguous where to put this, in v4.1 and v5.0 it is listed as
     *      optional in the PMIX_SESSION_INFO_ARRAY but according to the
     *      description it defaults to the job realm)
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
     * optional infos (v4.1 and v5.0):
     * * PMIX_HOSTNAME_KEEP_FQDN "pmix.fqdn" (bool)
     *     FQDNs are being retained by the PMIx library.
     *
     * * PMIX_ANL_MAP "pmix.anlmap" (char*)
     *     Process map expressed in ANLs PMI-1/PMI-2 notation.
     *
     * * PMIX_TDIR_RMCLEAN "pmix.tdir.rmclean" (bool)
     *     Resource Manager will cleanup assigned temporary directory trees.
     *
     * * PMIX_CRYPTO_KEY "pmix.sec.key" (pmix_byte_object_t)
     *     Blob containing crypto key.
     */

    INFO_LIST_CONVERT(list, jobInfo);
}

static void fillAppInfoArray(pmix_data_array_t *appInfo, PspmixApp_t *app)
{
    void *list = PMIx_Info_list_start();

    /* application number */
    INFO_LIST_ADD(list, PMIX_APPNUM, &app->num, PMIX_UINT32);

    /* number of processes in this application */
    INFO_LIST_ADD(list, PMIX_APP_SIZE, &app->size, PMIX_UINT32);

    /* PMIx v4.1 and v5.0 mentioning PMIX_MAX_PROCS here:
     * Maximum number of processes that can be executed in the specified realm.
     * Typically, this is a constraint imposed by a scheduler or by user
     * settings in a hostfile or other resource description.
     * Defaults to the job realm. Requires use of the PMIX_APP_INFO attribute
     * to avoid ambiguity when retrieving it.
     * @todo Clarify whether this should be set here specifically or each app */

    /* lowest rank in this application within the job */
    INFO_LIST_ADD(list, PMIX_APPLDR, &app->firstRank, PMIX_PROC_RANK);

    /* working directory for spawned processes */
    INFO_LIST_ADD(list, PMIX_WDIR, app->wdir, PMIX_STRING);

    /* concatenated argv for spawned processes */
    INFO_LIST_ADD(list, PMIX_APP_ARGV, app->args, PMIX_STRING);

    /* optional infos (PMIx v4.1 and v5.0):
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

    INFO_LIST_CONVERT(list, appInfo);
}

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
    void *list = PMIx_Info_list_start();

    /* node id (in the session) */
    INFO_LIST_ADD(list, PMIX_NODEID, &id, PMIX_UINT32);

    /* hostname */
    INFO_LIST_ADD(list, PMIX_HOSTNAME, node->hostname, PMIX_STRING);

    /* number of processes on the node (in this namespace) */
    INFO_LIST_ADD(list, PMIX_LOCAL_SIZE, &node->procs.len, PMIX_UINT32);

    /* Note: PMIX_NODE_SIZE (processes over all the user's jobs)
     * managed by PMIx_server_register_resources @todo
     * https://github.com/pmix/pmix-standard/issues/401*/

    /* lowest rank on this node within this job/namespace */
    PspmixProcess_t *proc = vectorGet(&node->procs, 0, PspmixProcess_t);
    INFO_LIST_ADD(list, PMIX_LOCALLDR, &proc->rank, PMIX_PROC_RANK);

    /* Comma-delimited list of ranks that are executing on the node
     * within this namespace */
    char *lpeers;
    lpeers = getNodeRanksString(node);
    if (lpeers[0] == '\0') flog("no local ranks for node %u (%s)\n", id,
				node->hostname);
    INFO_LIST_ADD(list, PMIX_LOCAL_PEERS, lpeers, PMIX_STRING);
    ufree(lpeers);

    /* optional infos (PMIx v4.1 and v5.0):
     * * PMIX_MAX_PROCS "pmix.max.size" (uint32_t)
     *     Maximum number of processes that can be executed in the specified
     *     realm. Typically, this is a constraint imposed by a scheduler or by
     *     user settings in a hostfile or other resource description. Defaults
     *     to the job realm.
     */

    if (node->id == PSC_getMyID()) {

	/* Full path to the top-level temporary directory assigned to the
	 * session */
	INFO_LIST_ADD(list, PMIX_TMPDIR, tmpdir, PMIX_STRING);

	/* Full path to the temporary directory assigned to the specified job,
	 * under PMIX_TMPDIR. */
	INFO_LIST_ADD(list, PMIX_NSDIR, nsdir, PMIX_STRING);

	/* Array of pmix_proc_t of all processes executing on the local node */
	/* @todo how to implement that, standard ambiguous?
	 *
	 * PMIX_LOCAL_PROCS "pmix.lprocs" (pmix_proc_t array)
	 * Array of pmix_proc_t of all processes executing on the local node –
	 * shortcut for PMIx_Resolve_peers for the local node and a NULL
	 * namespace argument. The process identifier is ignored for this
	 * attribute. */

	/* optional infos (PMIx v4.1 and v5.0):
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

    INFO_LIST_CONVERT(list, nodeInfo);
}

static void fillProcDataArray(pmix_data_array_t *procData,
			      PspmixProcess_t *proc, PSnodes_ID_t nodeID,
			      pmix_proc_t *spawnparent, const char *nsdir)
{
    void *list = PMIx_Info_list_start();

    bool spawned = spawnparent ? true : false;

    /* process rank within the job, starting from zero */
    INFO_LIST_ADD(list, PMIX_RANK, &proc->rank, PMIX_PROC_RANK);

    /* application number within the job in which the process is a member. */
    INFO_LIST_ADD(list, PMIX_APPNUM, &proc->app->num, PMIX_UINT32);

    /* rank within the process' application */
    INFO_LIST_ADD(list, PMIX_APP_RANK, &proc->arank, PMIX_PROC_RANK);

    /* rank of the process spanning across all jobs in this session
     * starting with zero.
     * Note that no ordering of the jobs is implied when computing this value.
     * As jobs can start and end at random times, this is defined as a
     * continually growing number - i.e., it is not dynamically adjusted as
     * individual jobs and processes are started or terminated. */
    INFO_LIST_ADD(list, PMIX_GLOBAL_RANK, &proc->grank, PMIX_PROC_RANK);

    /* rank of the process on its node in its job
     * refers to the numerical location (starting from zero) of the process on
     * its node when counting only those processes from the same job that share
     * the node, ordered by their overall rank within that job. */
    INFO_LIST_ADD(list, PMIX_LOCAL_RANK, &proc->lrank, PMIX_UINT16);

    /* rank of the process on its node spanning all jobs
     * refers to the numerical location (starting from zero) of the process on
     * its node when counting all processes (regardless of job) that share the
     * node, ordered by their overall rank within the job. The value represents
     * a snapshot in time when the specified process was started on its node and
     * is not dynamically adjusted as processes from other jobs are started or
     * terminated on the node. */
    INFO_LIST_ADD(list, PMIX_NODE_RANK, &proc->nrank, PMIX_UINT16);

    /* node identifier where the process is located */
    uint32_t val_u32 = nodeID;
    INFO_LIST_ADD(list, PMIX_NODEID, &val_u32, PMIX_UINT32);

    /* true if this proc resulted from a call to PMIx_Spawn */
    INFO_LIST_ADD(list, PMIX_SPAWNED, &spawned, PMIX_BOOL);

    /* parent process if this is the result of a call to PMIx_Spawn() */
    if (spawned) {
	INFO_LIST_ADD(list, PMIX_PARENT_ID, spawnparent, PMIX_PROC);
    }

    /* number of times this process has been re-instantiated
     * i.e, a value of zero indicates that the process has never been restarted.
     */
    INFO_LIST_ADD(list, PMIX_REINCARNATION, &proc->reinc, PMIX_UINT32);

    /* optional infos (PMIx v4.1 and v5.0):
     *
     * * PMIX_HOSTNAME "pmix.hname" (char*)
     *     Name of the host where the specified process is running.
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
	    flog("failed to generate locality string for rank %d: %s\n",
		 proc->rank, PMIx_Error_string(status));
	    locstr = ustrdup("pspmix:generation_error");
	    if (!locstr) abort(); /* @todo handle somehow more gently? */
	}
	INFO_LIST_ADD(list, PMIX_LOCALITY_STRING, locstr, PMIX_STRING);
	ufree(locstr);

	/* Full path to the subdirectory under PMIX_NSDIR assigned to the
	 * specified process. */
	int pdsize = strlen(nsdir)+10;
	char *procdir = umalloc(pdsize);
	if (snprintf(procdir, pdsize, "%s/%u", nsdir, proc->rank) >= pdsize) {
	    flog("warning: procdir truncated");
	}
	INFO_LIST_ADD(list, PMIX_PROCDIR, procdir, PMIX_STRING);
	ufree(procdir);

	/* rank of the process on the package (socket) where this process
	 * resides refers to the numerical location (starting from zero) of the
	 * process on its package when counting only those processes from the
	 * same job that share the package, ordered by their overall rank within
	 * that job. Note that processes that are not bound to PUs within a
	 * single specific package cannot have a package rank. */
	uint16_t pkgrank = 0; /* @todo how to get this here? */
	INFO_LIST_ADD(list, PMIX_PACKAGE_RANK, &pkgrank, PMIX_UINT16);
    }

    INFO_LIST_CONVERT(list, procData);
}

/**
 * To be called by PMIx_register_namespace() to provide status
 */
static void registerNamespace_cb(pmix_status_t status, void *cbdata)
{
    mycbdata_t *data = cbdata;

    fdbg(PSPMIX_LOG_CALL, "\n");

    data->status = status;

    SET_CBDATA_AVAIL(data);
}

bool pspmix_server_registerNamespace(char *srv_nspace, pmix_rank_t srv_rank,
				     const char *nspace, const char *jobid,
				     uint32_t sessionId, uint32_t univSize,
				     uint32_t jobSize, pmix_proc_t *spawnparent,
				     pmix_rank_t grankOffset, uint32_t numNodes,
				     list_t *procMap, uint32_t numApps,
				     PspmixApp_t *apps, const char *tmpdir,
				     const char *nsdir, PSnodes_ID_t nodeID)
{
    if (mset(PSPMIX_LOG_CALL)) {
	flog("srv_nspace '%s' srv_rank %d nspace '%s' sessionId %u univSize %u"
	     " jobSize %u spawnparent ", srv_nspace, srv_rank, nspace,
	     sessionId, univSize, jobSize);
	mlog ("%s", spawnparent ? pmixProcStr(spawnparent) : "(null)");
	mlog(" numNodes %d numApps %u tmpdir '%s' nsdir '%s' nodeID %d\n",
	     numNodes, numApps, tmpdir, nsdir, nodeID);
    }

    pmix_status_t status;

    if (univSize < 1) {
	flog("bad parameter: univSize = %u (< 1)\n", univSize);
	return false;
    }
    if (jobSize < 1) {
	flog("bad parameter: jobSize = %u (< 1)\n", jobSize);
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
	flog("failed to setup application: %s\n", PMIx_Error_string(status));
	DESTROY_CBDATA(cbdata);
	return false;
    }
    /* wait until the callback function has filled cbdata */
    WAIT_FOR_CBDATA(cbdata);

    if (cbdata.status != PMIX_SUCCESS) {
	flog("callback from setup application failed: %s\n",
	     PMIx_Error_string(cbdata.status));
	DESTROY_CBDATA(cbdata);
	return false;
    }

    fdbg(PSPMIX_LOG_VERBOSE, "got %lu info entries from"
	 " PMIx_server_setup_application()\n", cbdata.ninfo);

    DESTROY_CBDATA(cbdata);

    /* TODO save or return the received data? */
#endif

    /* find this node in procMap */
    PspmixNode_t *mynode = findNodeInList(nodeID, procMap);
    if (!mynode) {
	fdbg(PSPMIX_LOG_INFOARR,
	     "namespace without local node (%u) in process map\n", nodeID);
    }

    /* fill infos */
    void *list = PMIx_Info_list_start();

    /* PMIx server namespace hosting this namespace */
    INFO_LIST_ADD(list, PMIX_SERVER_NSPACE, srv_nspace, PMIX_STRING);

    /* Rank of this PMIx server */
    INFO_LIST_ADD(list, PMIX_SERVER_RANK, &srv_rank, PMIX_PROC_RANK);

    /*
     * total num of processes in this job
     *
     * this seems to be required by OpenPMIx (at least v4) in order to
     * determine the parent's jobsize when re-spawned processes reside
     * on "new" nodes; for some reason identical information in the
     * namespace's PMIX_JOB_INFO_ARRAY is ignored
     */
    INFO_LIST_ADD(list, PMIX_JOB_SIZE, &jobSize, PMIX_UINT32);

    /* ===== session info array ===== */
    pmix_data_array_t sessionInfo = PMIX_DATA_ARRAY_STATIC_INIT;
    fillSessionInfoArray(&sessionInfo, sessionId, univSize);
    INFO_LIST_ADD(list, PMIX_SESSION_INFO_ARRAY, &sessionInfo,
		   PMIX_DATA_ARRAY);

    /* ===== job info array ===== */
    pmix_data_array_t jobInfo = PMIX_DATA_ARRAY_STATIC_INIT;
    fillJobInfoArray(&jobInfo, nspace, jobid, jobSize, jobSize, procMap,
		     numApps, grankOffset);
    INFO_LIST_ADD(list, PMIX_JOB_INFO_ARRAY, &jobInfo,
		   PMIX_DATA_ARRAY);

    /* ===== application info arrays ===== */
    for (uint32_t j = 0; j < numApps; j++) {
	pmix_data_array_t appInfo = PMIX_DATA_ARRAY_STATIC_INIT;
	fillAppInfoArray(&appInfo, &apps[j]);

	INFO_LIST_ADD(list, PMIX_APP_INFO_ARRAY, &appInfo, PMIX_DATA_ARRAY);
    }

    list_t *n;

    /* ===== node info arrays ===== */
    uint32_t nodeIdx = 0;
    list_for_each(n, procMap) {
	PspmixNode_t *node = list_entry(n, PspmixNode_t, next);
	pmix_data_array_t nodeInfo = PMIX_DATA_ARRAY_STATIC_INIT;
	fillNodeInfoArray(&nodeInfo, node, nodeIdx++, tmpdir, nsdir);

	INFO_LIST_ADD(list, PMIX_NODE_INFO_ARRAY, &nodeInfo, PMIX_DATA_ARRAY);
    }

    /* ===== process data ===== */

    /* information about all global ranks */
    list_for_each(n, procMap) {
	PspmixNode_t *node = list_entry(n, PspmixNode_t, next);
	for (size_t j = 0; j < node->procs.len; j++) {
	    PspmixProcess_t *proc = vectorGet(&node->procs, j, PspmixProcess_t);
	    pmix_data_array_t procData = PMIX_DATA_ARRAY_STATIC_INIT;
	    fillProcDataArray(&procData, proc, node->id, spawnparent, nsdir);

	    INFO_LIST_ADD(list, PMIX_PROC_DATA, &procData, PMIX_DATA_ARRAY);
	}
    }

    pmix_data_array_t info = PMIX_DATA_ARRAY_STATIC_INIT;
    INFO_LIST_CONVERT(list, &info);

    mycbdata_t cbdata;
    INIT_CBDATA(cbdata, 0);
    cbdata.info = info.array;
    cbdata.ninfo = info.size;

    /* Not using PMIX_DATA_ARRAY_DESTRUCT(&info) here since cbdata actually
     * steals all allocated data */

    /* debugging output of info values */
    if (mset(PSPMIX_LOG_INFOARR)) {
	flog("PMIx_server_register_nspace info:\n");
	for (size_t j = 0; j < cbdata.ninfo; j++) {
	    char * istr = PMIx_Info_string(&cbdata.info[j]);
	    mlog("   %s\n", istr);
	    free(istr);
	}
    }

    /* register namespace */
    status = PMIx_server_register_nspace(nspace, mynode ? mynode->procs.len : 0,
					 cbdata.info, cbdata.ninfo,
					 registerNamespace_cb, &cbdata);
    if (status == PMIX_OPERATION_SUCCEEDED) {
	goto reg_nspace_success;
    }

    if (status != PMIX_SUCCESS) {
	flog("PMIx_server_register_nspace() failed\n");
	goto reg_nspace_error;
    }
    WAIT_FOR_CBDATA(cbdata);

    if (cbdata.status != PMIX_SUCCESS) {
	flog("callback from register namespace failed: %s\n",
		PMIx_Error_string(cbdata.status));
	goto reg_nspace_error;
    }

reg_nspace_success:
    DESTROY_CBDATA(cbdata);
    return true;

reg_nspace_error:
    DESTROY_CBDATA(cbdata);
    return false;
}

bool pspmix_server_createPSet(const char *name, PspmixNamespace_t *ns,
			      bool filter(PspmixNode_t *, PspmixProcess_t *,
					  void *),
			      void *data)
{
    vector_t members;
    vectorInit(&members, 128, 128, pmix_proc_t);

    list_t *n;
    list_for_each(n, &ns->procMap) {
	PspmixNode_t *node = list_entry(n, PspmixNode_t, next);
	for (uint16_t r = 0; r < node->procs.len; r++) {
	    PspmixProcess_t *proc = vectorGet(&node->procs, r, PspmixProcess_t);
	    if (filter(node, proc, data)) {
		pmix_proc_t pmixproc;
		PMIX_PROC_LOAD(&pmixproc, ns->name, proc->rank);
		vectorAdd(&members, &pmixproc);
	    }
	}
    }

    /* do not create empty psets */
    if (!members.len) {
	fdbg(PSPMIX_LOG_PSET, "process set '%s' would be empty\n", name);
	vectorDestroy(&members);
	return true;
    }

    pmix_status_t status = PMIx_server_define_process_set(members.data,
							  members.len, name);
    vectorDestroy(&members);

    /* Standard says PMIX_SUCCESS should be returned, but OpenPMIx 4.2.2
     * might return PMIX_OPERATION_SUCCEEDED instead */
    if (status != PMIX_SUCCESS && status != PMIX_OPERATION_SUCCEEDED) {
	flog("failed to create process set '%s': %s\n", name,
	     PMIx_Error_string(status));
	return false;
    }
    fdbg(PSPMIX_LOG_PSET, "process set '%s' created\n", name);
    return true;
}

/**
 * To be called by PMIx_deregister_namespace() to provide status
 */
static void deregisterNamespace_cb(pmix_status_t status, void *cbdata)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    const char *errstr = "";
    if (status != PMIX_SUCCESS) errstr = PMIx_Error_string(status);

    pspmix_service_cleanupNamespace(cbdata, (status != PMIX_SUCCESS), errstr);
}

void pspmix_server_deregisterNamespace(const char *nsname, void *nsobject)
{
    fdbg(PSPMIX_LOG_CALL, "nspace '%s'\n", nsname);

    /* deregister namespace */
    PMIx_server_deregister_nspace(nsname, deregisterNamespace_cb, nsobject);
}

/**
 * Callback for a call of PMIx_server_setup_local_support()
 */
static void setupLocalSupport_cb(pmix_status_t status, void *cbdata)
{
    mycbdata_t *data = cbdata;

    fdbg(PSPMIX_LOG_CALL, "\n");

    data->status = status;

    SET_CBDATA_AVAIL(data);
}

/* run this function once per node */
bool pspmix_server_setupLocalSupport(const char *nspace)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

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
	flog("setting up the local support callback failed: %s\n",
	     PMIx_Error_string(status));
	DESTROY_CBDATA(cbdata);
	return false;
    }
    WAIT_FOR_CBDATA(cbdata);

    if (cbdata.status != PMIX_SUCCESS) {
	flog("callback from setup local support failed: %s\n",
	     PMIx_Error_string(cbdata.status));
	DESTROY_CBDATA(cbdata);
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

    fdbg(PSPMIX_LOG_CALL, "\n");

    data->status = status;

    SET_CBDATA_AVAIL(data);
}

/* run this function at the server once per client */
bool pspmix_server_registerClient(const char *nspace, int rank, int uid,
				  int gid, void *clientObject)
{
    fdbg(PSPMIX_LOG_CALL, "nspace '%s' rank %d uid %d gid %d\n", nspace,
	 rank, uid, gid);

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
	flog("registering client failed: %s\n", PMIx_Error_string(status));
	DESTROY_CBDATA(cbdata);
	return false;
    }
    WAIT_FOR_CBDATA(cbdata);

    if (cbdata.status != PMIX_SUCCESS) {
	flog("callback from register client failed: %s\n",
	     PMIx_Error_string(cbdata.status));
	DESTROY_CBDATA(cbdata);
	return false;
    }
    DESTROY_CBDATA(cbdata);

    return true;
}

/* run this function in exception case to remove all client information  */
void pspmix_server_deregisterClient(const char *nspace, int rank)
{
    fdbg(PSPMIX_LOG_CALL, "nspace '%s' rank %d\n", nspace, rank);

    /* setup process struct */
    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    PMIX_PROC_LOAD(&proc, nspace, rank);

    /* deregister client */
    PMIx_server_deregister_client(&proc, NULL, NULL);
    PMIX_PROC_DESTRUCT(&proc);
}

/* get the client environment set */
bool pspmix_server_setupFork(const char *nspace, int rank, char ***childEnv)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    pmix_status_t status;

    /* setup process struct */
    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    PMIX_PROC_LOAD(&proc, nspace, rank);

    /* setup environment */
    status = PMIx_server_setup_fork(&proc, childEnv);
    PMIX_PROC_DESTRUCT(&proc);
    if (status != PMIX_SUCCESS) {
	flog("setting up environment for fork failed: %s\n",
	     PMIx_Error_string(status));
	return false;
    }

    return true;
}

bool pspmix_server_finalize(void)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    pmix_status_t status;

    if (!initialized) {
	flog("pspmix server not initialized\n");
	return false;
    }

    /* deregister the errhandler */
    PMIx_Deregister_event_handler(0, NULL, NULL);

    status = PMIx_server_finalize();
    if (status != PMIX_SUCCESS) {
	flog("PMIx_server_finalize() failed: %s\n", PMIx_Error_string(status));
	return false;
    }
    return true;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
