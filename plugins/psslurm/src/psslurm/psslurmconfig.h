/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_SLURM_CONFIG
#define __PS_SLURM_CONFIG

#include "pluginconfig.h"

#define SPOOL_DIR LOCALSTATEDIR "/spool/parastation"

/** The configuration list. */
Config_t Config;

Config_t SlurmConfig;

extern const ConfDef_t CONFIG_VALUES[];

int initConfig(char *filename);

#endif
