/*
 * ParaStation
 *
 * Copyright (C) 2010-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psmomlist.h"

#include <stdio.h>
#include <string.h>

#include "pluginmalloc.h"

#include "psmomlog.h"

static int insertEntry(list_t *list, char *name, char *res, char *val)
{
    Data_Entry_t *entry = umalloc(sizeof(*entry));

    entry->name = (!name || !strlen(name)) ? NULL : ustrdup(name);
    entry->resource = (!res || !strlen(res)) ? NULL : ustrdup(res);
    entry->value = (!val || !strlen(val)) ? NULL : ustrdup(val);

    list_add_tail(&entry->list, list);

    return 1;
}

void clearDataList(list_t *list)
{
    if (!list || list_empty(list)) return;

    list_t *d, *tmp;
    list_for_each_safe(d, tmp, list) {
	Data_Entry_t *dEntry = list_entry(d, Data_Entry_t, list);

	ufree(dEntry->name);
	ufree(dEntry->resource);
	ufree(dEntry->value);
	list_del(&dEntry->list);
	ufree(dEntry);
    }
    list_del(list);
}

void printDataEntry(Data_Entry_t *data)
{
    list_t *d;

    if (!data || list_empty(&data->list)) return;

    list_for_each(d, &data->list) {
	Data_Entry_t *dEntry = list_entry(d, Data_Entry_t, list);

	if (!dEntry->name || *dEntry->name == '\0') break;

	mlog("    %s : %s : %s\n", dEntry->name,
	     dEntry->resource, dEntry->value);
    }
}

static Data_Entry_t *findDataEntry(list_t *list, char *name, char *resource)
{
    list_t *d;

    if (!list || list_empty(list) || !name) return NULL;

    list_for_each(d, list) {
	Data_Entry_t *dEntry = list_entry(d, Data_Entry_t, list);

	if (!dEntry->name || strcmp(dEntry->name, name)) continue;

	if (!resource && !dEntry->resource) return dEntry;
	if (!resource) continue;

	if ((dEntry->resource && !strcmp(dEntry->resource, resource)) ||
	    (!dEntry->resource && !strlen(resource))) return dEntry;
    }

    return NULL;
}

char *getValue(list_t *list, char *name, char *resource)
{
    Data_Entry_t *data = findDataEntry(list, name, resource);
    if (!data) return NULL;

    return data->value;
}

int setEntry(list_t *list, char *name, char *resource, char *value)
{
    Data_Entry_t *dat = findDataEntry(list, name, resource);

    if (!dat) {
	insertEntry(list, name, resource, value);
    } else {
	if (value && dat->value && !strcmp(value, dat->value)) return 1;

	if (!value || !strlen(value)) {
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
