/*
 * ParaStation
 *
 * Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "list.h"
#include "psnodes.h"
#include "timer.h"

#include "plugin.h"
#include "psidhook.h"
#include "psidplugin.h"
#include "psidutil.h"

#define EXTENDED_API

#ifdef EXTENDED_API
int requiredAPI = 101;
#else
int requiredAPI = 100;
#endif

char name[] = "plugin1";

int version = 100;

plugin_dep_t dependencies[] = {
    // { "plugin2", 0 },
    { NULL, 0 } };

/* Flag suppressing some messages */
char *silent = NULL;

/* Flag suppressing all messages */
char *quiet = NULL;

int nodeUp(void *arg)
{
    PSID_log(-1, "%s/%s: ID %d\n", name, __func__, *(PSnodes_ID_t *)arg);
    return 0;
}

int nodeDown(void *arg)
{
    PSID_log(-1, "%s/%s: ID %d\n", name, __func__, *(PSnodes_ID_t *)arg);
    return 0;
}

#ifdef EXTENDED_API
int initialize(FILE *logfile)
{
    if (!silent && !quiet) PSID_log(-1, "%s: %s()\n", name, __func__);
    PSIDhook_add(PSIDHOOK_NODE_UP, nodeUp);
    PSIDhook_add(PSIDHOOK_NODE_DOWN, nodeDown);
    return 0;
}

int myTimer = -1;

void unload(void)
{
    if (!silent && !quiet) PSID_log(-1, "%s: %s()\n", name, __func__);
    PSIDplugin_unload(name);
}

void finalize(void)
{
    struct timeval timeout = {7, 0};

    if (!silent && !quiet) PSID_log(-1, "%s: %s()\n", name, __func__);

    myTimer = Timer_register(&timeout, unload);
    if (!silent||!quiet) PSID_log(-1, "%s: timer %d\n", name, myTimer);
}

void cleanup(void)
{
    if (!silent && !quiet) PSID_log(-1, "%s: %s()\n", name, __func__);

    if (myTimer > -1) {
	Timer_remove(myTimer);
	if (!silent||!quiet) PSID_log(-1, "%s: timer %d del\n", name, myTimer);
	myTimer = -1;
    }
    PSIDhook_del(PSIDHOOK_NODE_UP, nodeUp);
    PSIDhook_del(PSIDHOOK_NODE_DOWN, nodeDown);
}
#endif

char * help(void)
{
    char *helpText = "\tThis is the help-text for this plugin. For test-usage\n"
	"\tthis one includes more that one line. In fact, it even contains a\n"
	"\tthird line.\n";

    /* char *helpText = "\t" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" */
    /* 	"aaaaaaaaaa" "aaaaaaa"; */

    return strdup(helpText);
}

typedef struct {
    list_t next;
    char *key;
    char *val;
} keyVal_t;

/** List of key-value pairs currently known */
static LIST_HEAD(keyValList);

static keyVal_t * findEnt(char *key)
{
    list_t *t;

    if (!key) return NULL;

    list_for_each(t, &keyValList) {
	keyVal_t *kv = list_entry(t, keyVal_t, next);

	if (!strcmp(kv->key, key)) return kv;
    }

    return NULL;
}

char * set(char *key, char *val)
{
    keyVal_t *kv = findEnt(key);

    if (kv) {
	free(kv->val);
	kv->val = NULL;
	if (val) kv->val = strdup(val);
    } else {
	kv = malloc(sizeof(*kv));
	kv->key = strdup(key);
	kv->val = strdup(val);
	list_add_tail(&kv->next, &keyValList);
    }
    if (!kv || !kv->key || !kv->val) return strdup("\tNot enough memory\n");
    if (!strcmp(key, "magic")) return strdup("\tMagic value triggered\n");

    return NULL;
}

char * unset(char *key)
{
    keyVal_t *kv = findEnt(key);
    char retStr[128];

    if (!kv) {
	snprintf(retStr, sizeof(retStr), "\t%s: key '%s' not found\n", __func__,
		 key);
	return strdup(retStr);
    } else {
	list_del(&kv->next);
	free(kv->key);
	free(kv->val);
	free(kv);
    }

    return NULL;
}

char * show(char *key)
{
    char retStr[4096] = {'\0'};
    int filled = 0;

    if (key && *key) {
	keyVal_t *kv = findEnt(key);
	filled = 1;
	if (!kv) {
	    snprintf(retStr, sizeof(retStr), "\t%s: key '%s' not found\n",
		     __func__, key);
	} else {
	    snprintf(retStr, sizeof(retStr), "\t%s=%s\n", kv->key, kv->val);
	}
    } else {
	list_t *t;
	list_for_each(t, &keyValList) {
	    keyVal_t *kv = list_entry(t, keyVal_t, next);

	    filled = 1;
	    snprintf(retStr+strlen(retStr), sizeof(retStr)-strlen(retStr),
		     "\t%s=%s\n", kv->key, kv->val);
	}
    }

    if (filled) {
	return strdup(retStr);
    } else return NULL;
}

__attribute__((constructor))
void plugin_init(void)
{
    silent = getenv("PLUGIN_SILENT");
    quiet = getenv("PLUGIN_QUIET");

    if (!quiet) PSID_log(-1, "%s: %s()\n", name, __func__);
}

__attribute__((destructor))
void plugin_fini(void)
{
    if (!quiet) PSID_log(-1, "%s: %s()\n", name, __func__);
}
