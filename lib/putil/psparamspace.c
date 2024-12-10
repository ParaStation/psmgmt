/*
 * ParaStation
 *
 * Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psparamspace.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "list.h"
#include "pscommon.h"


/** Structure holding all information concerning a parameter */
typedef struct {
    list_t next;                 /**< Used to put into @ref paramList */
    char *name;                  /**< Actual name */
    void *data;                  /**< Pointer to ancillary data */
    PSPARM_setFunc_t *set;       /**< Modify parameter's value */
    PSPARM_printFunc_t *print;   /**< Print parameter's value */
    PSPARM_printFunc_t *help;    /**< Print help on parameter */
    keylist_t *keys;             /**< Possible values of this parameter */
} param_t;

/** List of parameters currently defined */
static LIST_HEAD(paramList);

PSPARM_setFunc_t *PSPARM_intSet;
PSPARM_setFunc_t *PSPARM_uintSet;
PSPARM_printFunc_t *PSPARM_intPrint;
PSPARM_setFunc_t *PSPARM_boolSet;
PSPARM_printFunc_t *PSPARM_boolPrint;
PSPARM_setFunc_t *PSPARM_stringSet;
PSPARM_printFunc_t *PSPARM_stringPrint;

static char * intSet(void *data, char *value)
{
    if (!data) return strdup("No place to store");

    char *end;
    long num = strtol(value, &end, 0);
    if (*end != '\0') {
	char line[80];
	snprintf(line, sizeof(line), "failed to parse '%s'", value);
	return strdup(line);
    }
    *(int *)data = num;

    return NULL;
}

static char * uintSet(void *data, char *value)
{
    int val = 0;
    char *ret = intSet(&val, value);
    if (ret) return ret;

    if (val < 0) {
	char line[80];
	snprintf(line, sizeof(line), "'%s' is not unsigned", value);
	return strdup(line);
    }
    *(unsigned int *)data = val;

    return NULL;
}

static char * intPrint(void *data)
{
    char *ret = NULL;

    if (data) {
	char line[80];
	snprintf(line, sizeof(line), "%d", *(int *)data);
	ret = strdup(line);
    }
    return ret;
}

static char * boolSet(void *data, char *value)
{
    if (!data) return strdup("No place to store");

    if (!value) return strdup("value missing");

    if (strcasecmp(value, "true")==0) {
	*(int *)data = 1;
    } else if (strcasecmp(value, "false")==0) {
	*(int *)data = 0;
    } else if (strcasecmp(value, "yes")==0) {
	*(int *)data = 1;
    } else if (strcasecmp(value, "no")==0) {
	*(int *)data = 0;
    } else {
	char *end;
	long num = strtol(value, &end, 0);
	if (*end != '\0') {
	    char line[80];
	    snprintf(line, sizeof(line), "failed to parse '%s'", value);
	    return strdup(line);
	}
	*(int *)data = !!num;
    }

    return NULL;
}

static char * boolPrint(void *data)
{
    char *ret = NULL;
    if (data) ret = strdup(*(int *)data ? "TRUE" : "FALSE");

    return ret;
}

static char * stringSet(void *data, char *value)
{
    if (!data) return strdup("No place to store");

    char **strp = data;
    free(*strp);

    if (value) *strp = strdup(value);
    if (value && ! *strp) return strdup("strdup() failed");

    return NULL;
}

static char * stringPrint(void *data)
{
    char **strp = data;
    if (strp && *strp) return strdup(*strp);

    return NULL;
}


void PSPARM_init(void)
{
    PSPARM_intSet = intSet;
    PSPARM_uintSet = uintSet;
    PSPARM_intPrint = intPrint;

    PSPARM_boolSet = boolSet;
    PSPARM_boolPrint = boolPrint;

    PSPARM_stringSet = stringSet;
    PSPARM_stringPrint = stringPrint;
}

/**
 * @brief Find parameter
 *
 * Find a parameter by its name @a name from the list of parameters @ref
 * paramList.
 *
 * @param name Name of the parameter to find.
 *
 * @return If the parameter was found, a pointer to the describing
 * structure is returned. Or NULL otherwise.
 */
static param_t * findParam(char *name)
{
    if (!name || ! *name) return NULL;

    list_t *p;
    list_for_each(p, &paramList) {
	param_t *param = list_entry(p, param_t, next);

	if (!strcmp(name, param->name)) return param;
    }

    return NULL;
}

/**
 * @brief Create new parameter structure
 *
 * Create and initialize a new parameter structure. The structure will
 * describe the new paramter of name @a name.
 *
 * This function ensures the parameter to be unique. I.e. if @a name
 * was registered before, an error is returned.
 *
 * The structure will already be included into the list parameters
 * @ref paramList at the alphabetically correct position.
 *
 * @param name Name of the parameter.
 *
 * @return Return the newly created structure, or NULL, if some error
 * occurred. This includes being name not unique in the parameter-space.
 */
static param_t * newParam(char *name)
{
    if (!name || ! *name) return NULL;

    param_t *param = findParam(name);
    if (param) {
	PSC_flog("parameter '%s' not unique\n", name);
	return NULL;
    }

    param = malloc(sizeof(*param));
    if (!param) {
	PSC_fwarn(errno, "malloc()");
	return NULL;
    }

    param->name = strdup(name);

    list_t *p;
    list_for_each(p, &paramList) {
	param_t *pp = list_entry(p, param_t, next);

	if (strcmp(pp->name, name)>0) break;
    }

    list_add_tail(&param->next, p);

    return param;
}

/**
 * @brief Delete parameter
 *
 * Delete the parameter @a param. After passing some consistency-tests
 * all the memory occupied by the describing structure are freed.
 *
 * @param param The param to be deleted
 *
 * @return No return value.
 */
static void delParam(param_t *param)
{
    if (!param) return;

    list_del(&param->next);
    free(param->name);
    free(param);
}


void PSPARM_finalize(void)
{
    list_t *p, *tmp;
    list_for_each_safe(p, tmp, &paramList) {
	param_t *param = list_entry(p, param_t, next);

	delParam(param);
    }
}


bool PSPARM_register(char *name, void *data, PSPARM_setFunc_t setFunc,
		     PSPARM_printFunc_t printFunc, PSPARM_printFunc_t helpFunc,
		     keylist_t *keys)
{
    if (!name || ! *name) {
	PSC_flog("no name given\n");
	return false;
    }

    param_t *param = newParam(name);
    if (!param) return false;

    param->data = data;
    param->set = setFunc;
    param->print = printFunc;
    param->help = helpFunc;
    param->keys = keys;

    return true;
}

bool PSPARM_remove(char *name)
{
    if (!name || ! *name) {
	PSC_flog("no name given\n");
	return false;
    }

    param_t *param = findParam(name);
    if (!param) {
	PSC_flog("unknown parameter '%s'\n", name);
	return false;
    }

    delParam(param);

    return true;
}

keylist_t PSPARM_boolKeys[] = {
    {"yes", NULL, NULL},
    {"no", NULL, NULL},
    {"true", NULL, NULL},
    {"false", NULL, NULL},
    {NULL, NULL, NULL}
};


keylist_t * PSPARM_getKeylist(void)
{
    /* count number of entries */
    int idx = 0;
    list_t *p;
    list_for_each(p, &paramList) idx++;

    keylist_t *keyList = malloc((idx+1)*sizeof(*keyList));
    if (!keyList) return NULL;

    idx = 0;
    list_for_each(p, &paramList) {
	param_t *param = list_entry(p, param_t, next);

	keyList[idx].key = strdup(param->name);
	if (!keyList[idx].key) {
	    while (--idx >= 0) free(keyList[idx].key);
	    free(keyList);
	    return NULL;
	}
	keyList[idx].action = NULL;
	keyList[idx].next = param->keys;

	idx++;
    }

    keyList[idx].key = NULL;
    keyList[idx].action = NULL;
    keyList[idx].next = NULL;

    return keyList;
}

void PSPARM_freeKeylist(keylist_t *keylist)
{
    if (!keylist) return;

    for (int i = 0; keylist[i].key; i++) free(keylist[i].key);
    free(keylist);
}

void PSPARM_set(char *name, char *value)
{
    if (!name || ! *name) {
	PSC_flog("no name given\n");
	return;
    }

    param_t *param = findParam(name);
    if (!param) {
	PSC_flog("unknown parameter '%s'\n", name);
    } else if (!param->set) {
	PSC_flog("no set-method for '%s'\n", name);
    } else {
	char *msg = param->set(param->data, value);
	if (msg) {
	    PSC_flog("%s: %s\n", param->name, msg);
	    free(msg);
	}
    }
}

char * PSPARM_get(char *name)
{
    char *res = NULL;
    if (name && *name) {
	param_t *param = findParam(name);

	if (param && param->print) res = param->print(param->data);
    }
    return res;
}

/**
 * @brief Print parameter
 *
 * Print the parameter @a param to the file @a file. For this, @a
 * param's print method is called in order to fetch the corresponding
 * information.
 *
 * If @a file or @a param is NULL, nothing is done.
 *
 * @param file The file to use for output
 *
 * @param param The parameter to print
 *
 * @return No return value
 */
static void printParam(FILE *file, param_t *param)
{

    if (!param || !file) return;

    fprintf(file, "%10s  ", param->name);
    char *res = param->print ? param->print(param->data) : NULL;
    if (res) {
	fprintf(file, "%s\n", res);
	free(res);
    } else {
	fprintf(file, "No data available\n");
    }
}

void PSPARM_print(FILE *file, char *name)
{
    if (name && *name) {
	param_t *param = findParam(name);

	if (param) {
	    printParam(file ? file : stdout, param);
	} else {
	    fprintf(file ? file : stdout, "%10s  Unknown parameter\n", name);
	}
    } else {
	list_t *p;
	list_for_each(p, &paramList) {
	    param_t *param = list_entry(p, param_t, next);
	    printParam(file ? file : stdout, param);
	}
    }
}

char * PSPARM_getHelp(char *name)
{
    char *res = NULL;
    if (name && *name) {
	param_t *param = findParam(name);

	if (param && param->help) res = param->help(param->data);
    }
    return res;
}

/**
 * @brief Print indented help message
 *
 * Print the a parameter's help-message @a helpStr using the indention
 * @a indent.
 *
 * The output format is as follows: It is expected that some output
 * leading to the indention @a indent is already created.
 *
 * If the output to be generated from @a helpStr does not fit within
 * one line, it is wrapped at suitable positions. For this purpose an
 * indentation of size @a indent is taken into account. Thus, @a indent
 * is expected to be (much) smaller than the length of the
 * actual line.
 *
 * Suitable positions for a line wrap are whitespace (' ') characters.
 * Leading whitespace at the beginning of a wrapped line - apart from
 * the indentation - will be skipped.
 *
 * In order to create a proper formatting of the output it is
 * necessary to determine the width of the terminal to print
 * on. Therefore, it does not make much sene to print to a different
 * destination than stdout. If you require to print to a different
 * destination, try to get the corresponding help-messages using @ref
 * PSPARM_getHelp() and create the output manually.
 *
 * @param indent The indent of the help-message to print
 *
 * @param helpStr The actual help-message to print
 *
 * @return No return value.
 */
static void printHelp(const int indent, char *helpStr)
{
    int lwidth = PSC_getWidth() - indent;

    if (helpStr) {
	char *pos = helpStr;
	int len = strlen(pos);

	while (len > lwidth) {
	    char *end = pos + lwidth - 1;
	    while (*end != ' ') end--;
	    printf("%.*s\n", (int)(end-pos), pos);
	    printf("%*s", PSC_getWidth()-lwidth, "");
	    pos = end+1;             /* Ignore the separating space */
	    len = strlen(pos);
	}
	printf("%.*s\n", lwidth, pos);
    }
    return;
}

/**
 * @brief Print actual help on parameter
 *
 * Print actual help-message on the parameter @a param to
 * stdout. Implicitely this uses the parameter's help-method in order
 * to create some output.
 *
 * @param param The parameter help is requested for
 *
 * @return No return value.
 */
static void printParamHelp(param_t *param)
{
    if (!param) return;

    int indent = printf("%10s  ", param->name);
    char *res = param->help ? param->help(param->data) : NULL;
    if (res) {
	printHelp(indent, res);
    } else {
	printf("No help available\n");
    }
}

void PSPARM_printHelp(char *name)
{
    if (name && *name) {
	param_t *param = findParam(name);
	if (param) {
	    printParamHelp(param);
	} else {
	    printf("%10s  Unknown parameter\n", name);
	}
    } else {
	list_t *p;
	list_for_each(p, &paramList) {
	    param_t *param = list_entry(p, param_t, next);
	    printParamHelp(param);
	}
    }
}
