%{
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <syslog.h>

#include "parse.h"

#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char yaccid[] __attribute__(( unused )) = "$Id: psid.scan.y,v 1.9 2002/02/15 19:35:25 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

 %}


%union{
  int val;
  unsigned char *string;
}

%token <val> NUMBER
%token <string> HOSTNAME 
%token <string> FILENAME 
%token <string> KEY

%token NL COMMENT

%token NROFNODES INSTDIR MODULE ROUTINGFILE

%token HWTYPE MYRINET ETHERNET NONE

%token LICENSEKEY LICENSESERVER

%token SMALLPACKET RESENDTIMEOUT HNPEND ACKPEND

%token MCASTGROUP MCASTPORT RDPPORT PSIDSELECTTIME DECLAREDEAD
%token RLIMITDATASIZE

%token SYSLOGLEVEL SYSLOG
%token PSLOG_KERN PSLOG_DAEMON
%token PSLOG_LOCAL0 PSLOG_LOCAL1 PSLOG_LOCAL2 PSLOG_LOCAL3 
%token PSLOG_LOCAL4 PSLOG_LOCAL5 PSLOG_LOCAL6 PSLOG_LOCAL7
%%
file:   /* empty */  
        | file listline
        ;

listline: NL
        | COMMENT
        | commline COMMENT
        | commline NL
        ;

commline: instdirline
        | nodesline
        | hostlist
        | psiddeclaredeadintervalline
        | psidselecttimeline
        | licenseline
        | moduleline
        | routingline
        | smallpacketline
        | resendtimeoutline
        | hnpendline
        | ackpendline
        | rlimitdatasizeline
        | sysloglevelline
        | syslogline
        | mcastportline
        | mcastgroupline
        | rdpportline
        ;

instdirline:
        INSTDIR HOSTNAME          { strcpy(ConfigInstDir,$2); }
        | INSTDIR FILENAME        { strcpy(ConfigInstDir,$2); }
        ;

nodesline: 
        NROFNODES NUMBER         { setNrOfNodes($2); }
        ;

hostlist:
        HOSTNAME NUMBER          { installHost(getlasthname(),$2); }
        ;

psiddeclaredeadintervalline:
        DECLAREDEAD NUMBER       { ConfigDeclareDeadInterval = $2;}
        ;

psidselecttimeline:
        PSIDSELECTTIME NUMBER    { ConfigPsidSelectTime = $2;}
        ;

licenseline:
        LICENSEKEY KEY           { strcpy(ConfigLicensekey,$2);}
        | LICENSESERVER HOSTNAME { installHost(getlasthname(),NrOfNodes); }
        ;

moduleline:
        MODULE HOSTNAME          { strcpy(ConfigModule,$2); }
        | MODULE FILENAME        { strcpy(ConfigModule,$2); }
        ;

routingline:
        ROUTINGFILE HOSTNAME     { strcpy(ConfigRoutefile,$2); }
        | ROUTINGFILE FILENAME   { strcpy(ConfigRoutefile,$2); }
        ;

smallpacketline:
        SMALLPACKET NUMBER       { ConfigSmallPacketSize = $2; }
        ;

resendtimeoutline:
        RESENDTIMEOUT NUMBER     { ConfigRTO = $2; }
        ;

hnpendline:
        HNPEND NUMBER            { ConfigHNPend = $2; }
        ;

ackpendline:
        ACKPEND NUMBER           { ConfigAckPend = $2; }
        ;

rlimitdatasizeline:
        RLIMITDATASIZE NUMBER    { ConfigRLimitDataSize = $2; }
        ;

sysloglevelline:
        SYSLOGLEVEL NUMBER       { ConfigSyslogLevel = $2; }
        ;

syslogline:
        SYSLOG NUMBER            { ConfigSyslog=$2; }
        | SYSLOG PSLOG_KERN      { ConfigSyslog=LOG_KERN; }
        | SYSLOG PSLOG_DAEMON    { ConfigSyslog=LOG_DAEMON; }
        | SYSLOG PSLOG_LOCAL0    { ConfigSyslog=LOG_LOCAL0; }
        | SYSLOG PSLOG_LOCAL1    { ConfigSyslog=LOG_LOCAL1; }
        | SYSLOG PSLOG_LOCAL2    { ConfigSyslog=LOG_LOCAL2; }
        | SYSLOG PSLOG_LOCAL3    { ConfigSyslog=LOG_LOCAL3; }
        | SYSLOG PSLOG_LOCAL4    { ConfigSyslog=LOG_LOCAL4; }
        | SYSLOG PSLOG_LOCAL5    { ConfigSyslog=LOG_LOCAL5; }
        | SYSLOG PSLOG_LOCAL6    { ConfigSyslog=LOG_LOCAL6; }
        | SYSLOG PSLOG_LOCAL7    { ConfigSyslog=LOG_LOCAL7; }
        ;

rdpportline:
        RDPPORT NUMBER           { ConfigRDPPort = $2; }
        ;

mcastgroupline: 
        MCASTGROUP NUMBER        { ConfigMCastGroup = $2; }
        ;

mcastportline:
        MCASTPORT NUMBER         { ConfigMCastPort = $2; }
        ;

%%

int lineno=0;

/***************/
int yywrap(void){
/***************/
  return 1;
}

/*******************/
void yyerror(char *s)
/*******************/
{
  fprintf(stderr,"%s in configuration file at line %d\n",s,lineno);
}

unsigned char lasthname[80];

/*******************/
void install(char *s)
/*******************/
{
  strcpy(lasthname,s);
  return;
}

/**********************/
char *getlasthname(void)
/**********************/
{
  return (char *)lasthname;
}
