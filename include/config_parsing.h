/*
 *               ParaStation3
 * config_parsing.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: config_parsing.h,v 1.14 2003/07/04 09:28:44 eicker Exp $
 *
 */
/**
 * \file
 * Parser for the config file of the ParaStation daemon
 *
 * $Id: config_parsing.h,v 1.14 2003/07/04 09:28:44 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PARSE_H
#define __PARSE_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

#include <sys/resource.h>
#include "pslic.h"

/** @todo Documentation */

extern char *ConfigInstDir;

extern struct env_fields_s ConfigLicEnv;

extern long ConfigSelectTime;
extern long ConfigDeadInterval;
extern long ConfigLicDeadInterval;
extern int ConfigRDPPort;
extern int ConfigMCastGroup;
extern int ConfigMCastPort;

extern int ConfigLogLevel;
extern int ConfigLogDest;

int parseConfig(int usesyslog, int loglevel, char *configfile);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PARSE_H */
