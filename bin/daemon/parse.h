#ifndef __parse_h__
#define __parse_h__

struct psihosttable{
  char found;
  unsigned int inet;
  char *name;
};

extern struct psihosttable *psihosttable;
extern char **hosttable;

extern int NrOfNodes;
extern int ConfigNo;

extern long ConfigPsidSelectTime;
extern long ConfigDeclareDeadInterval;

extern char ConfigLicensekey[];
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
void setnrofnodes(int n);

int parse_config(int syslogerror);

#endif
