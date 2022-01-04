/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psmomauth.h"

#include <stdio.h>

#include "pluginmalloc.h"

Auth_t AuthList;

void initAuthList()
{
    INIT_LIST_HEAD(&AuthList.list);
}

int isAuthIP(unsigned long ipaddr)
{
    struct list_head *pos;
    Auth_t *auth;

    if (list_empty(&AuthList.list)) return 0;

    list_for_each(pos, &AuthList.list) {
	if ((auth = list_entry(pos, Auth_t, list)) == NULL) return 0;
	if (auth->ipaddr == ipaddr) return 1;
    }
    return 0;
}

void addAuthIP(unsigned long ipaddr)
{
    Auth_t *auth;

    if (isAuthIP(ipaddr)) return;

    auth = (Auth_t *) umalloc(sizeof(Auth_t));
    auth->ipaddr = ipaddr;

    list_add_tail(&(auth->list), &AuthList.list);
}

void clearAuthList()
{
    list_t *pos, *tmp;
    Auth_t *auth;

    if (list_empty(&AuthList.list)) return;

    list_for_each_safe(pos, tmp, &AuthList.list) {
	if ((auth = list_entry(pos, Auth_t, list)) == NULL) return;

	list_del(&auth->list);
	ufree(auth);
    }
}
