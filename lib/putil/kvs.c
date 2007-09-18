/*
 *               ParaStation
 *
 * Copyright (C) 2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 *
 */
/**
 * \file
 * kvs.c: ParaStation key value space
 *
 * $Id$ 
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "env.h"
#include "psidpmicomm.h"

#include "kvs.h"

typedef struct {
    char *Name;
    env_fields_t *env;
} KVS_t;

KVS_t kvs[MAX_KVS];

/** 
 * @brief Finds a free position in the kvs structure
 *
 * @return Returns the free position if found, or -1 if the structure
 * is fully occupied.
 */
static int findFreeKvs(void)
{
    int i;
    
    for (i=0; i<MAX_KVS; i++) {
	if (!kvs[i].Name) {
	   return i;
	}
    }

    return -1;
}


/** 
 * @brief Searches the kvs structure for a certain name.
 *
 * @param name The name of the kvs.
 *
 * @return If a kvs with the specifyed name was found, it returns a
 * pointer to that kvs. If no kvs was found it returns 0.
 */
static KVS_t *getKvsByName(char *name)
{
    int i;
    
    if (!name || strlen(name) > KVSNAME_MAX ) {
	fprintf(stderr, "%s : invalid name\n", __func__);
	return 0;
    }

    for (i=0; i<MAX_KVS; i++) {
	if (kvs[i].Name && !strcmp(kvs[i].Name,name)) {
	   return &kvs[i];
	}
    }

    return 0;
}

/** 
 * @brief Init the kvs structure. This must be called bevor any other
 * kvs call.
 *
 * @return No return value.
 */
void kvs_init(void)
{
    int i;
    for (i=0; i<MAX_KVS; i++) {
	if ( kvs[i].Name ) {
	    kvs[i].Name = NULL;
	}
    }
}

/** 
 * @brief Creates a new kvs with the specifyed name.
 *
 * @param name Name of the kvs to create.
 *
 * @return Returns 0 on success, and 1 if an error occured.
 */
int kvs_create(char *name)
{
    int in;
    
    if (!name || strlen(name) < 1 || strlen(name) > KVSNAME_MAX) {
	fprintf(stderr, "%s : invalid name\n", __func__);
	return 1;
    }

    /* check if kvs with this name already exsists */
    if (getKvsByName(name)) {
	return 1;
    }

    /* find free kvs space */
    if ((in = findFreeKvs()) == -1) {
	fprintf(stderr,"Max number of kvs is already in use\n");
	return 1;
    }

    /* setup up the env */    
    if (!(kvs[in].env = malloc(sizeof(env_fields_t)))) {
        fprintf(stderr,"error malloc\n");
	exit(1);
    }
    env_init(kvs[in].env);

    /* set the name of the kvs */
    if (!(kvs[in].Name = malloc(strlen(name) +1))) {
	fprintf(stderr,"error malloc\n");
	exit(1);
    }
    strcpy(kvs[in].Name, name);

    return 0;
}

/** 
 * @brief Destroys a kvs with the specifyed name.
 *
 * @param name The name of the kvs to destroy.
 *
 * @return Returns 0 on success, and 1 if an error occured.
 */
int kvs_destroy(char *name)
{
    KVS_t *lkvs; 

    if (!name || strlen(name) < 1) {
	fprintf(stderr, "%s : invalid name\n", __func__);
	return 1;
    }
    
    lkvs = getKvsByName(name);
    
    /* kvs not found */
    if (!lkvs) {
	fprintf(stderr, "%s : delete to non existing kvs\n", __func__);
	return 1;
    }
    
    if (lkvs->Name) {
	free(lkvs->Name);
	lkvs->Name = NULL;
    }
    if (lkvs->env) {
	free(lkvs->env);
    }

    return 0;
}

/** 
 * @brief Saves a value in the kvs.
 *
 * @param kvsname The name of the kvs.
 *
 * @param name The name of the value to save.
 *
 * @param value The value to save in the kvs.
 *
 * @return Returns 0 on success, and 1 if an error occured.
 */
int kvs_put(char *kvsname, char *name, char *value)
{
    KVS_t *lkvs;
     
    if (!kvsname || !name || !value || strlen(kvsname) > KVSNAME_MAX
	|| strlen(name) > KEYLEN_MAX || strlen(value) > VALLEN_MAX ) {
	fprintf(stderr,
		"%s : invalid kvsname, valuename or value\n", __func__);
	return 1;
    }

    lkvs = getKvsByName(kvsname);
    
    /* kvs not found */
    if (!lkvs) {
	fprintf(stderr, "%s : put to non existing kvs\n", __func__);
	return 1;
    }
    
    if ((env_set(lkvs->env, name, value)) == -1) {
	fprintf(stderr, "%s : error while env_set\n", __func__);
	return 1;
    }

    return 0;
}

/** 
 * @brief Read a value from a kvs.
 *
 * @param kvsname The name of the kvs.
 *
 * @param name The name of the value to read.
 *
 * @return Returns the requested kvs value or 0 if an error occured.
 */
char *kvs_get(char *kvsname, char *name)
{
    KVS_t *lkvs;
    
    if (!kvsname || !name || strlen(kvsname) < 1 || strlen(name) < 1) {
	fprintf(stderr, "%s : invalid kvsname or valuename\n", __func__);
	return 0;
    }

    lkvs = getKvsByName(kvsname);
    
    /* kvs not found */
    if (!lkvs) {
	fprintf(stderr, "%s : kvs not found name:%s\n", __func__, kvsname);
	return 0;
    }
    
    return env_get(lkvs->env, name);
}

/** 
 * @brief Count the values in a kvs.
 *
 * @param The name of the kvs.
 *
 * @return Returns the number of values or -1 if an error occured.
 */
int kvs_count_values(char *kvsname)
{
    KVS_t *lkvs;

    if (!kvsname || strlen(kvsname) < 1 ) {
	fprintf(stderr, "%s : invalid kvsname \n", __func__);
	return -1;
    }
    
    lkvs = getKvsByName(kvsname);
    
    /* kvs not found */
    if (!lkvs) {
	fprintf(stderr, "%s : count to non existing kvs\n", __func__);
	return -1;
    }

    return env_size(lkvs->env);
}

/** 
 * @brief Count the number of kvs created.
 *
 * @return The number of kvs created.
 */
int kvs_count(void)
{
    int i, count = 0;

    for (i=0; i<MAX_KVS; i++) {
	if (kvs[i].Name) {
	    count++;	
	}
    }

    return count;
}


/** 
 * @brief Read a value by index from kvs.
 *
 * @param kvsname The name of the kvs.
 *
 * @param index The index of the kvs value.
 *
 * @return Returns the value in format name=value or 0 if an error
 * occured.
 */
char *kvs_getbyidx(char *kvsname, int index)
{
    KVS_t *lkvs;

    if (!kvsname || strlen(kvsname) < 1) {
	fprintf(stderr, "%s : invalid kvsname\n", __func__);
	return 0;
    }
    
    lkvs = getKvsByName(kvsname);
    
    /* kvs not found */
    if (!lkvs) {
	fprintf(stderr, "%s : getbyidx to non existing kvs\n", __func__);
	return 0;
    }
    
    return env_dump(lkvs->env, index);
}


/** 
 * @brief Read the name of a kvs by index. 
 *
 * @param index The index of the kvs.
 *
 * @return Returns the name of a kvs by index or 0 on error.
 */
char *kvs_getKvsNameByIndex(int index)
{
    if(index >= MAX_KVS) {
	return 0;
    }

    return kvs[index].Name;
}


