/*
 *               ParaStation3
 * config_parsing.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: config_parsing.h,v 1.5 2002/07/11 10:44:39 eicker Exp $
 *
 */
/**
 * \file
 * parse: Parser for the config file of the ParaStation daemon
 *
 * $Id: config_parsing.h,v 1.5 2002/07/11 10:44:39 eicker Exp $
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
#include "pstask.h"

/** @todo Documentation */

/* Hashed host table needed for reverse lookup (ip-addr given, determine id) */
struct host_t{
    unsigned int addr;
    int id;
    struct host_t* next;
};

extern struct host_t *hosts[256];  /* host table */

extern int NrOfNodes;

/* List of all nodes, info about hardware included */
struct node_t{
    unsigned int addr;     /* IP address of that node */
    short numCPU;          /* Number of CPUs in that node */
    char isUp;             /* Actual status of that node */
    unsigned int hwType;   /* Communication hardware on that node */
    unsigned int hwStatus; /* Corresponding stati of the hardware */
    int hasIP;             /* Flag to mark that node to load the ip-module */
    int starter;           /* Flag to allow to start jobs from that node */
    PStask_t* tasklist;    /* tasklist of that node */
};

extern struct node_t *nodes;

extern struct node_t licNode;

extern char *Configfile;
extern char *ConfigInstDir;

extern char *ConfigLicenseKey;

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

int parser_lookupHost(unsigned int ipaddr);

int parseConfig(int usesyslog, int loglevel);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PARSE_H */
