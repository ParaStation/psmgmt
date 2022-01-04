/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Helper functions for psconfig handling
 */
#ifndef __PSCONFIG_HELPER_H
#define __PSCONFIG_HELPER_H

#ifndef BUILD_WITHOUT_PSCONFIG
#include <glib.h>
#include <psconfig.h>

/**
 * @brief Create psconfig object for further retrievals
 *
 * Try to construct a local configuration object for the database @a
 * db suitable for future local information retrievals. To identify
 * the corresponding object @a flags are taken into account.
 *
 * To setup the object this function tries to identify the "NodeName"
 * for the object of the form "host:<gethostname()>". If this fails,
 * it retries with a cut hostname, i.e. shortened by trailing full
 * qualified domain name section ("all the stuff behind the first
 * dot").
 *
 * @param db psconfig database to utilize
 *
 * @param flags Flags steering the behavior of the database
 *
 * @return On success the name of the local configuration object
 * suitable for further calls to the database is returned; in case of
 * failure NULL is returned
 */
char * PSCfgHelp_getObject(PSConfig* db, guint flags);

#endif /* BUILD_WITHOUT_PSCONFIG */

#endif /* __PSCONFIG_HELPER_H */
