/*
 *               ParaStation3
 * parse.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: parse.h,v 1.8 2002/04/22 22:51:11 hauke Exp $
 *
 */
/**
 * \file
 * parse: Parser for ParaStation daemon
 *
 * $Id: parse.h,v 1.8 2002/04/22 22:51:11 hauke Exp $
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

#define yylex psidlex
#define yyin psidin

struct psihosttable{
    char found;
    unsigned int inet;
    char *name;
};

typedef enum {myrinet, ethernet, none} HWType;

extern struct psihosttable *psihosttable;

extern char *Configfile;

extern int NrOfNodes;

extern char ConfigInstDir[];
extern char ConfigLicensekey[];
extern char ConfigModule[];
extern char ConfigRoutefile[];

extern int ConfigSmallPacketSize;
extern int ConfigRTO;
extern int ConfigHNPend;
extern int ConfigAckPend;

extern long ConfigPsidSelectTime;
extern long ConfigDeclareDeadInterval;
extern int ConfigRDPPort;
extern int ConfigMCastGroup;
extern int ConfigMCastPort;
extern int ConfigRLimitDataSize;

extern int ConfigSyslogLevel;
extern int ConfigSyslog;

extern HWType ConfigHWType;

extern int MyPsiId;
extern unsigned int MyId;

void installHost(char *s,int n);
void setNrOfNodes(int n);

int parseConfig(int syslogerror);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PARSE_H */
