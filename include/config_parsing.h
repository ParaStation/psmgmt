/*
 *               ParaStation3
 * parse.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: config_parsing.h,v 1.2 2002/06/13 17:32:32 eicker Exp $
 *
 */
/**
 * \file
 * parse: Parser for ParaStation daemon
 *
 * $Id: config_parsing.h,v 1.2 2002/06/13 17:32:32 eicker Exp $
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
#include "psitask.h"

typedef enum {none, myrinet, ethernet} HWType;

/* Hashed host table needed for reverse lookup (ip-addr given, determine id) */
struct host_t{
    unsigned int addr;
    int id;
    struct host_t* next;
};

extern struct host_t *hosts[256];  /* host table */

/* List of all nodes, info about hardware included */
struct node_t{
    unsigned int addr;     /* IP address of that node */
    char status;           /* Actual status of that node */
    HWType hwtype;         /* Communication hardware on that node */
    int ip;                /* Flag to mark that node to have the ip-module */
    int starter;           /* Flag to allow to start jobs from that node */
    PStask_t* tasklist;    /* tasklist of that node */
};

extern struct node_t *nodes;


extern char *Configfile;

extern int NrOfNodes;

extern char *ConfigInstDir;
extern char *ConfigLicensekey;
extern char *ConfigModule;
extern char *ConfigRoutefile;

extern int ConfigSmallPacketSize;
extern int ConfigRTO;
extern int ConfigHNPend;
extern int ConfigAckPend;

extern long ConfigSelectTime;
extern long ConfigDeadInterval;
extern int ConfigRDPPort;
extern int ConfigMCastGroup;
extern int ConfigMCastPort;

extern rlim_t ConfigRLimitCPUTime;
extern rlim_t ConfigRLimitDataSize;
extern rlim_t ConfigRLimitStackSize;
extern rlim_t ConfigRLimitRSSSize;

extern int ConfigSyslogLevel;
extern int ConfigSyslog;

extern HWType ConfigHWType;

extern int MyPsiId;

int parser_lookupHost(unsigned int ipaddr);

int parseConfig(int syslogerror);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PARSE_H */
