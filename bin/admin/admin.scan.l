%{
#include "y.tab.h"
extern int lineno;

%}

%%
[ \t]+					{ BEGIN INITIAL; }
[0-9]+					{ sscanf(yytext,"%d",&yylval.val); return NUMBER; }
0x[0-9a-fA-F]+				{ sscanf(yytext,"%x",&yylval.val); return HEXNUMBER; }
ADD|add					{ return ADDOP; }
SET|set					{ return SETOP; }
NODENUMBER|nodenumber|NODENAME|nodename { return INFOOP; }
STAT|STATUS|stat|status|S|s		{ return STATOP; }
CONFIG|CONF|config|conf			{ return SHOWOP; }
KILL|kill				{ return KILLOP; }
MAXPROC|maxproc				{ return MAXPROC; }
SMALLPACKETSIZE|SmallPacketSize|smallpacketsize	{ return SMALLPACKETSIZE; }
RESENDTIMEOUT|ResendTimeout|resendtimeout	{ return RESENDTIMEOUT; }
DEBUGMASK|Debugmask|debugmask	{ return DEBUGMASK; }
SELECT|select				{ return SELECT; }
SLEEP|sleep				{ return SLEEP; }
NO|no   			        { return NO; }
NOMAXPROC|nomaxproc			{ return NOMAXPROC; }
USER|user				{ return USER; }
NOUSER|nouser				{ return NOUSER; }
NODE|node				{ return NODE; }
NET|Net|net				{ return NET; }
COUNT|Count|count			{ return COUNT; }
PROC|proc				{ return PROC; }
LOAD|load				{ return LOAD; }
RDP|rdp					{ return RDP; }
RDPDEBUG|rdpdebug			{ return RDPDEBUG; }
PSIDDEBUG|psiddebug			{ return PSIDDEBUG; }
NOPSIDDEBUG|nopsiddebug			{ return NOPSIDDEBUG; }
ALL|A|all|a|FULL|F|full|f		{ return ALL; }
RESTART|restart				{ return RESTARTOP; }
SHUTDOWN|shutdown			{ return SHUTDOWNOP; }
RESET|reset				{ return RESETOP; }
TEST|test				{ return TESTOP; }
QUIET|quiet				{ return QUIET; }
VERBOSE|verbose				{ return VERBOSE; }
NORMAL|normal				{ return NORMAL; }
EXIT|exit|QUIT|quit|e|q			{ return QUITOP; }
HELP|help|H|h|[?]			{ return HELPOP; }
VERSION|V|version|v			{ return VERSIONOP; }
DEBUG|debug                             { return DEBUGOP;}
[-a-zA-Z0-9_.]+				{ strcpy(yylval.name,yytext); return NAME; }
[\n]					{ return NULLOP; }
.					{ printf("PSIadmin: unrecognized characters in input: %s \n",yytext); }
%%

