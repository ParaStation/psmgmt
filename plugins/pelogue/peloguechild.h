/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef PS_PELOGUE_CHILD
#define PS_PELOGUE_CHILD

#include "peloguetypes.h"

#include "list.h"
#include "pluginforwarder.h"

#include "peloguechild.h"


/** the list head of the child list */
Child_t ChildList;

/**
 * @brief Initialize the child list.
 *
 * @return No return value.
 */
void initChildList(void);

/**
 * @brief Convert a child type to string.
 *
 * @param type The child type to convert.
 *
 * @return Returns the requested type as string or NULL on error.
 */
char *childType2String(int type);

/**
 * @brief Delete all children.
 *
 * @return No return value.
 */
void clearChildList(void);

/**
 * @brief Add a new child.
 *
 * @param pid The process pid of the child to add.
 *
 * @param type The type of the child.
 *
 * @param jobid The corresponding jobid of the child.
 *
 * @return Returns a pointer the new created child structure or NULL on error.
 */
Child_t *addChild(const char *plugin, char *jobid, Forwarder_Data_t *fwdata,
		    PELOGUE_child_types_t type);

/**
 * @brief Delete a child which is identified by its pid.
 *
 * @param pid The pid of the child to delete.
 *
 * @return Returns 0 on error and 1 on success.
 */
int deleteChild(const char *plugin, const char *jobid);

/**
 * @brief Find a child which is identified by its pid.
 *
 * @param pid The pid of the child to find.
 *
 * @return Returns a pointer to the child requested or NULL on error.
 */
Child_t *findChild(const char *plugin, const char *jobid);

#endif
