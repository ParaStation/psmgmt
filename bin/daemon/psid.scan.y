%{
#include <stdio.h>
#include <sys/types.h>
#include <syslog.h>

#include "parse.h"
%}


%union{
  int val;
  unsigned char *string;
}

%token <val> NUMBER PORT
%token <string> HOSTNAME 
%token NL COMMENT NROFNODES
%token LICENSEKEY LICENSESERVER ROUTINGFILE
%token DECLAREDEAD PSIDSELECTTIME SMALLPACKET RLIMITDATASIZE RESENDTIMEOUT
%token AT CONFIG MCAST SYSLOGLEVEL SYSLOG
%token PSLOG_KERN PSLOG_DAEMON
%token PSLOG_LOCAL0 PSLOG_LOCAL1 PSLOG_LOCAL2 PSLOG_LOCAL3 
%token PSLOG_LOCAL4 PSLOG_LOCAL5 PSLOG_LOCAL6 PSLOG_LOCAL7
%%
file:   /* empty */  
	| file listline
	;

listline: NL
	| COMMENT
	| nodesline COMMENT
	| nodesline NL
	| hostlist COMMENT
	| hostlist NL
	| psiddeclaredeadintervalline COMMENT
	| psiddeclaredeadintervalline NL
	| psidselecttimeline COMMENT
	| psidselecttimeline NL
	| licenseline COMMENT
	| licenseline NL
	| routingline COMMENT
	| routingline NL
	| smallpacketline COMMENT
	| smallpacketline NL
	| resendtimeoutline COMMENT
	| resendtimeoutline NL
	| rlimitdatasizeline COMMENT
	| rlimitdatasizeline NL
	| sysloglevelline COMMENT
	| sysloglevelline NL
	| syslogline COMMENT
	| syslogline NL
	| mcastline COMMENT
	| mcastline NL
	;

nodesline: 
	NROFNODES NUMBER         { setnrofnodes($2); }
	;

hostlist:
	HOSTNAME NUMBER          { installhost(getlasthname(),$2); }
	;

psiddeclaredeadintervalline:
	DECLAREDEAD NUMBER       { ConfigDeclareDeadInterval = $2;}
	;

psidselecttimeline:
	PSIDSELECTTIME NUMBER    { ConfigPsidSelectTime = $2;}
	;

licenseline:
	LICENSEKEY HOSTNAME      { strcpy(ConfigLicensekey,$2);}
        | LICENSESERVER HOSTNAME { installhost(getlasthname(),NrOfNodes); }
	;

routingline:
	ROUTINGFILE HOSTNAME     { strcpy(ConfigRoutefile,$2);}
	;

smallpacketline:
	SMALLPACKET NUMBER       { ConfigSmallPacketSize=$2; }
	;

resendtimeoutline:
	RESENDTIMEOUT NUMBER     { ConfigResendTimeout=$2; }
	;

rlimitdatasizeline:
	RLIMITDATASIZE NUMBER    { ConfigRLimitDataSize=$2; }
	;

sysloglevelline:
	SYSLOGLEVEL NUMBER       { ConfigSyslogLevel=$2; }
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

mcastline: 
	MCAST NUMBER		       { ConfigMgroup = $2; }
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

