/*
 *               ParaStation3
 * config_parsing.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: config_parsing.h,v 1.12 2003/03/06 14:09:57 eicker Exp $
 *
 */
/**
 * \file
 * Parser for the config file of the ParaStation daemon
 *
 * $Id: config_parsing.h,v 1.12 2003/03/06 14:09:57 eicker Exp $
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

extern char *Configfile;
extern char *ConfigInstDir;

extern struct env_fields_s ConfigLicEnv;
extern char *ConfigLicenseKeyMCP;
extern char *ConfigMyriModule;
extern char *ConfigRoutefile;
extern int ConfigSmallPacketSize;
extern int ConfigRTO;
extern int ConfigHNPend;
extern int ConfigAckPend;

extern char *ConfigIPModule;
extern char *ConfigIPPrefix;
extern int ConfigIPPrefixLen;

extern char *ConfigGigaEtherModule;

extern long ConfigSelectTime;
extern long ConfigDeadInterval;
extern long ConfigLicDeadInterval;
extern int ConfigRDPPort;
extern int ConfigMCastGroup;
extern int ConfigMCastPort;

extern rlim_t ConfigRLimitCPUTime;
extern rlim_t ConfigRLimitDataSize;
extern rlim_t ConfigRLimitStackSize;
extern rlim_t ConfigRLimitRSSSize;

extern int ConfigLogLevel;
extern int ConfigLogDest;

extern int MyPsiId;

int parseConfig(int usesyslog, int loglevel);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PARSE_H */
