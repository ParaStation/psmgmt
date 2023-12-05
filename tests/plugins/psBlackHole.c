/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "logging.h"
#include "psprotocol.h"

#include "plugin.h"
#include "pluginmalloc.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "psidutil.h"

int requiredAPI = 120;

#define NAME "psBlackHole"
char name[] = NAME;

int version = 100;

plugin_dep_t dependencies[] = {
    { NULL, 0 } };

#define nlog(...) if (PSID_logger) logger_funcprint(PSID_logger, name,	\
						    -1, __VA_ARGS__)

typedef struct {
    int16_t type;         /**< message type */
    int32_t subType;      /**< message (sub-)type */
} type_t;

typedef struct {
    list_t next;
    type_t type;
    int rate;
} dropper_t;

static LIST_HEAD(dropList);

dropper_t * findDropper(type_t type)
{
    list_t *d;

    list_for_each(d, &dropList) {
	dropper_t *dropper = list_entry(d, dropper_t, next);
	if (dropper->type.type == type.type
	    && dropper->type.subType == type.subType) return dropper;
    }

    return NULL;
}

int doRandomDrop(void *amsg)
{
    DDTypedBufferMsg_t *msg = amsg;
    list_t *d;

    list_for_each(d, &dropList) {
	dropper_t *dropper = list_entry(d, dropper_t, next);
	type_t type = dropper->type;
	if (msg->header.type == type.type
	    && (type.subType == -1 || type.subType == msg->type)) {
	    if (100.0*rand()/(RAND_MAX) <= dropper->rate) {
		return 0; // drop message
	    }
	}
    }

    return 1;
}


int initialize(FILE *logfile)
{
    /* register needed hooks */
    if (!PSIDhook_add(PSIDHOOK_RANDOM_DROP, doRandomDrop)) {
	nlog("'PSIDHOOK_RANDOM_DROP' registration failed\n");
	return 1;
    }
    PSIDcomm_enableDropHook(true);
    nlog("(%i) successfully started\n", version);

    return 0;
}


void cleanup(void)
{
    list_t *d, *tmp;

    PSIDcomm_enableDropHook(false);
    PSIDhook_del(PSIDHOOK_RANDOM_DROP, doRandomDrop);

    list_for_each_safe(d, tmp, &dropList) {
	dropper_t *dropper = list_entry(d, dropper_t, next);

	list_del(&dropper->next);
	free(dropper);
    }
}


char * help(char *key)
{
    char *helpText =
	"\tAllow to drop messages randomly for debugging purposes.\n"
	"\tUse the plugin's show directive to list all message types to drop:\n"
	"\t\tplugin show " NAME "\n"
	"\tUse the plugin's set directive to add new message types to drop\n"
	"\t\tplugin set " NAME " <type>[_<subtype>] <probability>\n"
	"\tUse the plugin's unset directive to stop dropping a message type\n"
	"\t\tplugin unset " NAME " <type>[_<subtype>]\n";

    return strdup(helpText);
}

char * show(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;
    list_t *d;
    char l[128];

    list_for_each(d, &dropList) {
	dropper_t *dropper = list_entry(d, dropper_t, next);
	type_t type = dropper->type;

	if (dropper->type.subType != -1) {
	    snprintf(l, sizeof(l), "\ttype %d_%d rate %d\n", type.type,
		     type.subType, dropper->rate);
	} else {
	    snprintf(l, sizeof(l), "\ttype %d rate %d\n", type.type,
		     dropper->rate);
	}
	str2Buf(l, &buf, &bufSize);
    }

    return buf;
}

static int getInt(char *str)
{
    long val;
    char *end;

    if (!str) return -1;

    val = strtol(str, &end, 0);
    if (*end != '\0') {
	return -1;
    }

    return val;
}

type_t getType(char *val)
{
    type_t type;
    char *myVal = strdup(val);
    char *subType = strchr(myVal, '_');

    if (subType) {
	*subType = '\0';
	subType++;
	type.subType = getInt(subType);
	if (type.subType == -1) {
	    type.type = -1;
	    free(myVal);
	    return type;
	}
    } else {
	type.subType = -1;
    }
    type.type = getInt(myVal);

    free(myVal);

    return type;
}

char * set(char *key, char *val)
{
    char l[128];

    if (!key || !key[0] || !val || !val[0]) {
	snprintf(l, sizeof(l), "\tUsage: 'plugin set %s <type>[_<subtype>]"
		 " <rate>' to drop messages\n", name);
	return strdup(l);
    }

    type_t type = getType(key);
    if (type.type == -1) {
	snprintf(l, sizeof(l), "\tIllegal type `%s`\n", key);
	return strdup(l);
    }

    int rate = getInt(val);
    if (rate < 0 || rate > 100) {
	snprintf(l, sizeof(l), "\tIllegal value `%s`\n", val);
	return strdup(l);
    }

    dropper_t *dropper = findDropper(type);
    if (!dropper) {
	dropper = malloc(sizeof(*dropper));
	if (!dropper) {
	    snprintf(l, sizeof(l), "\tNo memory\n");
	    return strdup(l);
	}
	dropper->type = type;
	dropper->rate = rate;
	list_add_tail(&dropper->next, &dropList);
    } else {
	dropper->rate = rate;
    }

    return NULL;
}

char * unset(char *key)
{
    char l[128];

    if (!key || !key[0]) {
	snprintf(l, sizeof(l), "\tUsage: 'plugin unset %s <type>[_subtype]\n",
		 name);
	return strdup(l);
    }

    type_t type = getType(key);
    if (type.type == -1) {
	snprintf(l, sizeof(l), "\tIllegal type `%s`\n", key);
	return strdup(l);
    }

    dropper_t *dropper = findDropper(type);
    if (!dropper) {
	snprintf(l, sizeof(l), "\tNo dropper found\n");
	return strdup(l);
    }
    list_del(&dropper->next);
    free(dropper);

    return NULL;
}
