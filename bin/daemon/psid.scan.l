%{
#include "y.tab.h"
extern int lineno;
static int _i,_j;
extern unsigned char lasthname[];
void install(char *s);
%}

%%
[ \t]+			;
#.*			{ return COMMENT; }
[0-9]+			{ sscanf(yytext,"%d",&yylval.val); return NUMBER; }
0x[0-9a-fA-F]+		{ sscanf(yytext,"%x",&yylval.val); return NUMBER; }
[0-9]+[.][0-9]+		{ sscanf(yytext,"%d.%d",&_i,&_j);yylval.val=(_i<<16)|_j; return PORT; }
0x[0-9a-fA-F]+[.][0-9]+	{ sscanf(yytext,"%x.%d",&_i,&_j);yylval.val=(_i<<16)|_j; return PORT; }
[0-9]+[.]0x[0-9a-fA-F]+	{ sscanf(yytext,"%d.%x",&_i,&_j);yylval.val=(_i<<16)|_j; return PORT; }
0x[0-9a-fA-F]+[.]0x[0-9a-fA-F]+	{ sscanf(yytext,"%x.%x",&_i,&_j);yylval.val=(_i<<16)|_j; return PORT; }
NrOfNodes|NrOfnodes|NrofNodes|Nrofnodes|nrOfNodes|nrOfnodes|nrofNodes|nrofnodes	{ return NROFNODES; }
mcast|Mcast|MCAST	      { return MCAST; }
declaredeadinterval|DeclareDeadInterval|DECLAREDEADINTERVAL {return DECLAREDEAD;}
psidselecttime|PsidSelectTime|PSIDSELECTTIME		{return PSIDSELECTTIME;}
licensekey|Licensekey|LICENSEKEY|LicenseKey		{return LICENSEKEY;}
routingfile|Routingfile|ROUTINGFILE|RoutingFile		{return ROUTINGFILE;}
licenseserver|Licenseserver|LICENSESERVER|LicenseServer	{return LICENSESERVER;}
smallpacketsize|SmallPacketSize|SMALLPACKETSIZE		{return SMALLPACKET;}
resendtimeout|ResendTimeout|RESENDTIMEOUT		{return RESENDTIMEOUT;}
rlimitdatasize|RLimitDataSize|RLIMITDATASIZE		{return RLIMITDATASIZE;}
sysloglevel|SyslogLevel|SysLogLevel|SYSLOGLEVEL		{return SYSLOGLEVEL;}
syslog|Syslog|SysLog|SYSLOG				{return SYSLOG;}
log_daemon|Log_Daemon|Log_daemon|LOG_DAEMON|daemon	{return PSLOG_DAEMON;}
log_kern|Log_Kern|Log_kern|LOG_KERN|kernel		{return PSLOG_KERN;}
log_local0|Log_Local0|Log_local0|LOG_LOCAL0|local0	{return PSLOG_LOCAL0;}
log_local1|Log_Local1|Log_local1|LOG_LOCAL1|local1	{return PSLOG_LOCAL1;}
log_local2|Log_Local2|Log_local2|LOG_LOCAL2|local2	{return PSLOG_LOCAL2;}
log_local3|Log_Local3|Log_local3|LOG_LOCAL3|local3	{return PSLOG_LOCAL3;}
log_local4|Log_Local4|Log_local4|LOG_LOCAL4|local4	{return PSLOG_LOCAL4;}
log_local5|Log_Local5|Log_local5|LOG_LOCAL5|local5	{return PSLOG_LOCAL5;}
log_local6|Log_Local6|Log_local6|LOG_LOCAL6|local6	{return PSLOG_LOCAL6;}
log_local7|Log_Local7|Log_local7|LOG_LOCAL7|local7	{return PSLOG_LOCAL7;}
at|At|AT|@		{return AT;}
[\n]			{ lineno++; return NL; }
[-a-zA-Z0-9_.:;<=?@]+	{ yylval.string=lasthname; install(yytext); return HOSTNAME; }

%%
