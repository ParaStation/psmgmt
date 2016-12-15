/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PELOGUE_CHILD
#define __PELOGUE_CHILD

#include <stdbool.h>

#include "peloguetypes.h"

/**
 * @brief Convert a child type to string.
 *
 * @param type The child type to convert.
 *
 * @return Returns the requested type as string or NULL on error.
 */
char *childType2String(PElogueType_t type);

/**
 * @brief Add a new child
 *
 * Add a new child of type @a type associated to the plugin @a plugin
 * and the job ID @a jobid.
 *
 * @param plugin Name of the plugin responsible for this child
 *
 * @param jobid Job ID responsible for starting this child
 *
 * @param type The type of the child to create
  *
 * @return Returns a pointer the newly created child or NULL on error
 */
PElogueChild_t *addChild(char *plugin, char *jobid, PElogueType_t type);

/**
 * @brief Find child identified by plugin and job ID
 *
 * Find a child in the list of childs identified by the associated
 * plugin @a plugin and its job ID @a jobid.
 *
 * @param plugin Name of the plugin responsible for this child
 *
 * @param jobid Job ID responsible for starting this child
 *
 * @return Returns a pointer to the child requested or NULL on error
 */
PElogueChild_t *findChild(const char *plugin, const char *jobid);

/**
 * @brief Delete child
 *
 * Delete the child @a child. This includes detaching from the
 * corresponding forwarder and freeing all memory.
 *
 * @param child Pointer to the child to delete
 *
 * @return Returns true on success and false on error
 */
bool deleteChild(PElogueChild_t *child);

/**
 * @brief Delete all children
 *
 * Delete all children in the list of children. This includes killing
 * all associated forwarders.
 *
 * @return No return value.
 */
void clearChildList(void);


#endif  /* __PELOGUE_CHILD */
