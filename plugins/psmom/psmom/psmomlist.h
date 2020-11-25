/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_MOM_LIST
#define __PS_MOM_LIST

#include "list.h"

typedef struct {
    char *name;
    char *resource;
    char *value;
    struct list_head list;
} Data_Entry_t;

int setEntry(struct list_head *list, char *name, char *resource, char *value);
int delDataEntr(Data_Entry_t *data);
void printDataEntry(Data_Entry_t *data);
char *getValue(struct list_head *list, char *name, char *resource);
void clearDataList(struct list_head *list);

#endif  /* __PS_MOM_LIST */
