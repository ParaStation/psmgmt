#ifndef __parse_h__
#define __parse_h__

struct psihosttable{
  char found;
  unsigned int inet;
  char *name;
};

extern struct psihosttable *psihosttable;
extern char **hosttable;

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
void setnrofnodes(int n);

int parse_config(int syslogerror);

#endif
