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
#include <time.h>

#include "pluginmalloc.h"

#include "psmomclient.h"

void initClientList()
{
    INIT_LIST_HEAD(&ClientList.list);
}

Client_t *findClient(unsigned long ipaddr)
{
    struct list_head *pos;
    Client_t *client;

    if (list_empty(&ClientList.list)) return 0;

    list_for_each(pos, &ClientList.list) {
	if ((client = list_entry(pos, Client_t, list)) == NULL) {
	    return 0;
	}
	if (client->ipaddr == ipaddr) {
	    return client;
	}
    }
    return 0;
}

Client_t *addClient(unsigned long ipaddr)
{
    Client_t *client;

    client = (Client_t *) umalloc(sizeof(Client_t));
    client->ipaddr = ipaddr;
    client->last_contact = time(NULL);

    list_add_tail(&(client->list), &ClientList.list);

    return client;
}

void updateClient(unsigned long ipaddr)
{
    Client_t *client;

    if (!(client = findClient(ipaddr))) {
	client = addClient(ipaddr);
    } else {
	client->last_contact = time(NULL);
    }
}
