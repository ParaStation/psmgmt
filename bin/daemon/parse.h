/*
 *               ParaStation3
 * parse.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: parse.h,v 1.5 2002/01/22 16:14:48 eicker Exp $
 *
 */
/**
 * \file
 * parse: Parser for ParaStation daemon
 *
 * $Id: parse.h,v 1.5 2002/01/22 16:14:48 eicker Exp $
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

struct psihosttable{
    char found;
    unsigned int inet;
    char *name;
};

extern struct psihosttable *psihosttable;

extern char *Configfile;

extern int NrOfNodes;

extern long ConfigPsidSelectTime;
extern long ConfigDeclareDeadInterval;

extern char ConfigInstDir[];
extern char ConfigLicensekey[];
extern char ConfigModule[];
extern char ConfigRoutefile[];
extern int ConfigSmallPacketSize;
extern int ConfigResendTimeout;
extern int ConfigRLimitDataSize;
extern int ConfigSyslogLevel;
extern int ConfigSyslog;
extern int ConfigMgroup;

extern int MyPsiId;
extern unsigned int MyId;

void installhost(char *s,int n);
void setNrOfNodes(int n);

int parseConfig(int syslogerror);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PARSE_H */
