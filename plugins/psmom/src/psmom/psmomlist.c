/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
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

#include <stdlib.h>
#include <stdio.h>

#include "pluginmalloc.h"
#include "string.h"

#include "psmomlog.h"
#include "psmomlist.h"

static int insertEntry(struct list_head *list, char *name,
						    char *resource, char *value)
{
    Data_Entry_t *entry;

    entry = (Data_Entry_t *) umalloc(sizeof(Data_Entry_t));
    entry->name =(!name || strlen(name) == 0 ) ? NULL : ustrdup(name);
    entry->resource = (!resource || strlen(resource) == 0 ) ?
						    NULL : ustrdup(resource);
    entry->value = (!value || strlen(value) == 0 ) ? NULL : ustrdup(value);

    list_add_tail(&(entry->list), list);
    return 1;
}

void clearDataList(struct list_head *list)
{
    Data_Entry_t *next;
    list_t *pos, *tmp;

    if (!list || list_empty(list)) return;

    list_for_each_safe(pos, tmp, list) {
	if ((next = list_entry(pos, Data_Entry_t, list)) == NULL) {
	    return;
	}
	if (next->name) ufree(next->name);
	if (next->resource) ufree(next->resource);
	if (next->value) ufree(next->value);
	list_del(&next->list);
	ufree(next);
    }
    list_del(list);
}

void printDataEntry(Data_Entry_t *data)
{
    Data_Entry_t *next;
    struct list_head *pos;

    if (!data || list_empty(&data->list)) return;

    list_for_each(pos, &data->list) {
	if ((next = list_entry(pos, Data_Entry_t, list)) == NULL) {
	    break;
	}
	if (!next->name || next->name == '\0') {
	    break;
	}
	mlog("    %s : %s : %s\n", next->name,
		next->resource, next->value);
    }
}

static Data_Entry_t *findDataEntry(struct list_head *list, char *name,
    char *resource)
{
    Data_Entry_t *next;
    struct list_head *pos;

    if (!list || list_empty(list) || !name) return NULL;

    list_for_each(pos, list) {
	if ((next = list_entry(pos, Data_Entry_t, list)) == NULL) {
	    break;
	}

	if (next->name && !(strcmp(next->name, name))) {
	    if (!resource && !next->resource) return next;
	    if (!resource) continue;
	    if ((next->resource && !(strcmp(next->resource, resource))) ||
		(!next->resource && strlen(resource) == 0)) {
		return next;
	    }
	}
    }
   return NULL;
}

char *getValue(struct list_head *list, char *name, char *resource)
{
    Data_Entry_t *data;

    if (!(data = findDataEntry(list, name, resource))) {
	return NULL;
    }
    return data->value;
}

int setEntry(struct list_head *list, char *name, char *resource, char *value)
{
    Data_Entry_t *dat;

    if (!(dat = findDataEntry(list, name, resource))) {
	insertEntry(list, name, resource, value);
    } else {
	if (value && dat->value && !(strcmp(value, dat->value))) return 1;

	if (!value || (strlen(value)) == 0) {
	    ufree(dat->value);
	    dat->value = NULL;
	    return 1;
	}

	if (!dat->value) {
	    dat->value = ustrdup(value);
	    return 1;
	}

	if (strlen(dat->value) >= strlen(value)) {
	    strcpy(dat->value, value);
	    return 1;
	}

	dat->value = urealloc(dat->value, strlen(value) + 1);
	strcpy(dat->value, value);
    }
    return 1;
}
