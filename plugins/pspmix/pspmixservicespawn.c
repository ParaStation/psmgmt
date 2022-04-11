/*
 * ParaStation
 *
 * Copyright (C) 2018-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pspmixservicespawn.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pstask.h"

#include "pluginmalloc.h"
#include "pluginstrv.h"

#include "pspmixlog.h"

/* spawn functions */
static fillerFunc_t *fillTaskFunction = NULL;

/** Generic staic buffer */
static char buffer[1024];

/* ************************************************************************* *
 *                           MPI Spawn Handling                              *
 * ************************************************************************* */

#if 0
/**
 * @brief Extracts the arguments from PMI spawn request
 *
 * @param msg Buffer containing the PMI spawn message
 *
 * @param argv String vector to add the extracted arguments to
 *
 * @return Returns true on success and false on error
 */
static bool getSpawnArgs(char *msg, strv_t *args)
{
    char *execname;
    char numArgs[50];
    int addArgs = 0, i;

    /* setup argv */
    if (!getpmiv("argcnt", msg, numArgs, sizeof(numArgs))) {
	mlog("%s(r%i): missing argc argument\n", __func__, rank);
	return false;
    }

    addArgs = atoi(numArgs);
    if (addArgs > PMI_SPAWN_MAX_ARGUMENTS) {
	mlog("%s(r%i): too many arguments (%i)\n", __func__, rank, addArgs);
	return false;
    } else if (addArgs < 0) {
	mlog("%s(r%i): invalid argument count (%i)\n", __func__, rank, addArgs);
	return false;
    }

    /* add the executable as argv[0] */
    execname = getpmivm("execname", msg);
    if (!execname) {
	mlog("%s(r%i): invalid executable name\n", __func__, rank);
	return false;
    }
    strvAdd(args, execname);

    /* add additional arguments */
    for (i = 1; i <= addArgs; i++) {
	char *nextval;
	snprintf(buffer, sizeof(buffer), "arg%i", i);
	nextval = getpmivm(buffer, msg);
	if (nextval) {
	    strvAdd(args, nextval);
	} else {
	    size_t j;
	    for (j = 0; j < args->count; j++) ufree(args->strings[j]);
	    mlog("%s(r%i): extracting arguments failed\n", __func__, rank);
	    return false;
	}
    }

    return true;
}
#endif

#if 0
/**
 * @brief Extract key-value pairs from PMI spawn request
 *
 * @param msg Buffer containing the PMI spawn message
 *
 * @param name Identifier for the key-value pairs to extract
 *
 * @param kvpc Where to store the number key-value pairs
 *
 * @param kvpv Where to store the array of key-value pairs
 *
 * @return Returns true on success, false on error
 */
static bool getSpawnKVPs(char *msg, char *name, int *kvpc, KVP_t **kvpv)
{
    char numKVP[50];
    int count, i;

    snprintf(buffer, sizeof(buffer), "%s_num", name);
    if (!getpmiv(buffer, msg, numKVP, sizeof(numKVP))) {
	mlog("%s(r%i): missing %s count\n", __func__, rank, name);
	return false;
    }
    *kvpc = atoi(numKVP);

    if (!*kvpc) return true;

    *kvpv = umalloc(*kvpc * sizeof(KVP_t));

    for (i = 0; i < *kvpc; i++) {
	char nextkey[PMI_KEYLEN_MAX], nextvalue[PMI_VALLEN_MAX];
	snprintf(buffer, sizeof(buffer), "%s_key_%i", name, i);
	if (!getpmiv(buffer, msg, nextkey, sizeof(nextkey))) {
	    mlog("%s(r%i): invalid %s key %s\n", __func__, rank, name, buffer);
	    goto kvp_error;
	}

	snprintf(buffer, sizeof(buffer), "%s_val_%i", name, i);
	if (!getpmiv(buffer, msg, nextvalue, sizeof(nextvalue))) {
	    mlog("%s(r%i): invalid %s val %s\n", __func__, rank, name, buffer);
	    goto kvp_error;
	}

	(*kvpv)[i].key = ustrdup(nextkey);
	(*kvpv)[i].value = ustrdup(nextvalue);
    }
    return true;

kvp_error:
    count = i;
    for (i = 0; i < count; i++) {
	ufree((*kvpv)[i].key);
	ufree((*kvpv)[i].value);
    }
    ufree(*kvpv);
    return false;
}
#endif

#if 0
/**
 * @brief Parse spawn request messages
 *
 * @param msg Buffer containing the PMI spawn request message
 *
 * @param spawn Pointer to the struct to be filled with spawn data
 *
 * @return Returns true on success, false on error
 */
static bool parseSpawnReq(char *msg, SingleSpawn_t *spawn)
{
    const char delm[] = "\n";
    strv_t args;
    char *tmpStr;

    if (!msg) return false;

    setPMIDelim(delm);

    /* get the number of processes to spawn */
    tmpStr = getpmivm("nprocs", msg);
    if (!tmpStr) {
	mlog("%s(r%i): getting number of processes to spawn failed\n",
	     __func__, rank);
	goto parse_error;
    }
    spawn->np = atoi(tmpStr);
    ufree(tmpStr);

    /* setup argv for processes to spawn */
    strvInit(&args, NULL, 0);
    if (!getSpawnArgs(msg, &args)) {
	strvDestroy(&args);
	goto parse_error;
    }
    spawn->argv = args.strings;
    spawn->argc = args.count;

    /* extract preput keys and values */
    if (!getSpawnKVPs(msg, "preput", &spawn->preputc, &spawn->preputv)) {
	goto parse_error;
    }

    /* extract info keys and values */
    if (!getSpawnKVPs(msg, "info", &spawn->infoc, &spawn->infov)) {
	goto parse_error;
    }

    setPMIDelim(NULL);
    return true;

parse_error:
    setPMIDelim(NULL);
    return false;
}
#endif

#if 0
/**
 * @brief Spawn one or more processes
 *
 * We first need the next rank for the new service process to
 * start. Only the logger knows that. So we ask it and buffer
 * the spawn request to wait for an answer.
 *
 * @param req Spawn request data structure
 *
 * @return Returns true on success and false on error
 */
static bool doSpawn(SpawnRequest_t *req)
{
    if (pendSpawn) {
	mlog("%s(r%i): anoter spawn is pending\n", __func__, rank);
	return false;
    }

    pendSpawn = copySpawnRequest(req);

    mlog("%s(r%i): trying to do %d spawns\n", __func__, rank, pendSpawn->num);

    /* get next service rank from logger */
    if (PSLog_write(cTask->loggertid, SERV_TID, NULL, 0) < 0) {
	mlog("%s(r%i): Writing to logger failed.\n", __func__, rank);
	return false;
    }

    return true;
}
#endif

#if 0
/**
 * @brief Spawn one or more processes
 *
 * Parses the spawn message and calls doSpawn().
 *
 * In case of spawn_multi, all spawn messages are collected here
 * and doSpawn() is called after the collection is completed.
 *
 * @param msg Buffer containing the PMI spawn message
 *
 * @return Returns 0 for success, 1 on normal error, 2 on critical
 * error, and 3 on fatal error
 */
static int handleSpawnRequest(char *msg)
{
    char buf[50];
    int totSpawns, spawnsSoFar;
    bool ret;

    static int s_total = 0;
    static int s_count = 0;

    if (!getpmiv("totspawns", msg, buf, sizeof(buf))) {
	mlog("%s(r%i): invalid totspawns argument\n", __func__, rank);
	return 1;
    }
    totSpawns = atoi(buf);

    if (!getpmiv("spawnssofar", msg, buf, sizeof(buf))) {
	mlog("%s(r%i): invalid spawnssofar argument\n", __func__, rank);
	return 1;
    }
    spawnsSoFar = atoi(buf);

    if (spawnsSoFar == 1) {
	s_total = totSpawns;

	/* check if spawn buffer is already in use */
	if (spawnBuffer) {
	    mlog("%s(r%i): spawn buffer should be empty, another spawn in"
		 " progress?\n", __func__, rank);
	    return 2;
	}

	/* create spawn buffer */
	spawnBuffer = initSpawnRequest(s_total);
	if (!spawnBuffer) {
	    mlog("%s(r%i): out of memory\n", __func__, rank);
	    return 3;
	}
    } else if (s_total != totSpawns) {
	mlog("%s(r%i): totalspawns argument does not match previous message\n",
	     __func__, rank);
	return 2;
    }

    if (debug) elog("%s(r%i): Adding spawn %d/%d to buffer\n", __func__, rank,
		    spawnsSoFar, totSpawns);

    if (!parseSpawnReq(msg, &(spawnBuffer->spawns[s_count]))) {
	mlog("%s(r%i): failed to parse spawn message '%s'\n",  __func__, rank,
	     msg);
	return 1;
    }

    if (!spawnBuffer->spawns[s_count].np) {
	mlog("%s(r%i): spawn %d/%d has (np == 0) set, cancel spawning.\n",
	     __func__, rank, s_count, spawnBuffer->num);
	elog("Rank %i: Spawn %d/%d has (np == 0) set, cancel spawning.\n",
	     rank, s_count, spawnBuffer->num);
    }

    s_count++;

    if (s_count < s_total) {
	/* another part of the multi spawn request is missing */
	return 0;
    }

    /* collection complete */
    s_total = 0;
    s_count = 0;

    /* do spawn */
    ret = doSpawn(spawnBuffer);

    freeSpawnRequest(spawnBuffer);
    spawnBuffer = NULL;

    if (!ret) {
	mlog("%s(r%i): doSpawn() failed\n",  __func__, rank);
	return 1;
    }

    /* wait for logger to answer */
    return 0;
}
#endif

/**
 * @brief Prepare preput keys and values to pass by environment
 *
 * Generates an array of strings representing the preput key-value-pairs in the
 * format "__PMI_<KEY>=<VALUE>" and one variable "__PMI_preput_num=<COUNT>"
 *
 * @param preputc Number of key value pairs
 *
 * @param preputv Array of key value pairs
 *
 * @param envv Where to store the pointer to the array of definitions
 *
 * @return No return value
 */
static void addPreputToEnv(int preputc, KVP_t *preputv, strv_t *env)
{
    int i;
    char *tmpstr;

    snprintf(buffer, sizeof(buffer), "__PMI_preput_num=%i", preputc);
    strvAdd(env, ustrdup(buffer));

    for (i = 0; i < preputc; i++) {
	int esize;

	snprintf(buffer, sizeof(buffer), "preput_key_%i", i);
	esize = 6 + strlen(buffer) + 1 + strlen(preputv[i].key) + 1;
	tmpstr = umalloc(esize);
	snprintf(tmpstr, esize, "__PMI_%s=%s", buffer, preputv[i].key);
	strvAdd(env, tmpstr);

	snprintf(buffer, sizeof(buffer), "preput_val_%i", i);
	esize = 6 + strlen(buffer) + 1 + strlen(preputv[i].value) + 1;
	tmpstr = umalloc(esize);
	snprintf(tmpstr, esize, "__PMI_%s=%s", buffer, preputv[i].value);
	strvAdd(env, tmpstr);
    }
}
/**
 *  fills the passed task structure to spawn processes using mpiexec
 *
 *  @param req spawn request
 *
 *  @param usize universe size
 *
 *  @param task task structure to adjust
 *
 *  @return 1 on success, 0 on error (currently unused)
 */
static int fillWithMpiexec(SpawnRequest_t *req, int usize, PStask_t *task)
{
    SingleSpawn_t *spawn;
    KVP_t *info;
    strv_t args, env;
    bool noParricide = false;
    char *tmpStr;
    int i, j;

    spawn = &(req->spawns[0]);

    /* put preput key-value-pairs into environment
     *
     * We will transport this kv-pairs using the spawn environment. When
     * our children are starting the pmi_init() call will add them to their
     * local KVS.
     *
     * Only the values of the first single spawn are used. */
    strvInit(&env, task->environ, task->envSize);
    addPreputToEnv(spawn->preputc, spawn->preputv, &env);

    ufree(task->environ);
    task->environ = env.strings;
    task->envSize = env.count;

    /* build arguments:
     * mpiexec -u <UNIVERSE_SIZE> -np <NP> -d <WDIR> -p <PATH> \
     *  --nodetype=<NODETYPE> --tpp=<TPP> <BINARY> ... */
    strvInit(&args, NULL, 0);

    tmpStr = getenv("__PSI_MPIEXEC_KVSPROVIDER");
    if (tmpStr) {
	strvAdd(&args, ustrdup(tmpStr));
    } else {
	strvAdd(&args, ustrdup(LIBEXECDIR "/kvsprovider"));
    }
    strvAdd(&args, ustrdup("-u"));

    snprintf(buffer, sizeof(buffer), "%d", usize);
    strvAdd(&args, ustrdup(buffer));

    for (i = 0; i < req->num; i++) {

	spawn = &(req->spawns[0]);

	/* set the number of processes to spawn */
	strvAdd(&args, ustrdup("-np"));
	snprintf(buffer, sizeof(buffer), "%d", spawn->np);
	strvAdd(&args, ustrdup(buffer));

	/* extract info values and keys
	 *
	 * These info variables are implementation dependend and can
	 * be used for e.g. process placement. All unsupported values
	 * will be silently ignored.
	 *
	 * ParaStation will support:
	 *
	 *  - wdir: The working directory of the spawned processes
	 *  - arch/nodetype: The type of nodes to be used
	 *  - path: The directory were to search for the executable
	 *  - tpp: Threads per process
	 *  - parricide: Flag to not kill process upon relative's unexpected
	 *               death.
	 *
	 * TODO:
	 *
	 *  - soft:
	 *	    - if not all processes could be started the spawn is still
	 *		considered successful
	 *	    - ignore negative values and >maxproc
	 *	    - format = list of triplets
	 *		+ list 1,3,4,5,8
	 *		+ triplets a:b:c where a:b range c= multiplicator
	 *		+ 0:8:2 means 0,2,4,6,8
	 *		+ 0:8:2,7 means 0,2,4,6,7,8
	 *		+ 0:2 means 0,1,2
	 */
	for (j = 0; j < spawn->infoc; j++) {
	    info = &(spawn->infov[j]);

	    if (!strcmp(info->key, "wdir")) {
		strvAdd(&args, ustrdup("-d"));
		strvAdd(&args, ustrdup(info->value));
	    }
	    if (!strcmp(info->key, "tpp")) {
		size_t len = strlen(info->value) + 7;
		tmpStr = umalloc(len);
		snprintf(tmpStr, len, "--tpp=%s", info->value);
		strvAdd(&args, tmpStr);
	    }
	    if (!strcmp(info->key, "nodetype") || !strcmp(info->key, "arch")) {
		size_t len = strlen(info->value) + 12;
		tmpStr = umalloc(len);
		snprintf(tmpStr, len, "--nodetype=%s", info->value);
		strvAdd(&args, tmpStr);
	    }
	    if (!strcmp(info->key, "path")) {
		strvAdd(&args, ustrdup("-p"));
		strvAdd(&args, ustrdup(info->value));
	    }

	    /* TODO soft spawn
	    if (!strcmp(info->key, "soft")) {
		char *soft;
		int *count, *sList;

		soft = info->value;
		sList = getSoftArgList(soft, &count);

		ufree(soft);
	    }
	    */

	    if (!strcmp(info->key, "parricide")) {
		if (!strcmp(info->value, "disabled")) {
		    noParricide = true;
		} else if (!strcmp(info->value, "enabled")) {
		    noParricide = false;
		}
	    }
	}

	/* add binary and argument from spawn request */
	for (j = 0; j < spawn->argc; j++) {
	    strvAdd(&args, ustrdup(spawn->argv[j]));
	}

	/* add separating colon */
	if (i < req->num - 1) {
	    strvAdd(&args, ustrdup(":"));
	}
    }

    task->argv = args.strings;
    task->argc = args.count;

    task->noParricide = noParricide;

    return 1;
}

void pspmix_setFillSpawnTaskFunction(fillerFunc_t spawnFunc)
{
    mdbg(PSPMIX_LOG_VERBOSE, "Set specific PMI fill spawn task function\n");
    fillTaskFunction = spawnFunc;
}

void pspmix_resetFillSpawnTaskFunction(void)
{
    mdbg(PSPMIX_LOG_VERBOSE, "Reset PMI fill spawn task function\n");
    fillTaskFunction = fillWithMpiexec;
}

fillerFunc_t * pspmix_getFillTaskFunction(void) {
    return fillTaskFunction;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
