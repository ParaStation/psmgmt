%{
#include "y.tab.h"
#include <string.h>
%}

%option reject

%%
[ \t]+			{ BEGIN INITIAL; }
[0-9]+			{ sscanf(yytext,"%d",&yylval.val); return NUMBER; }
0x[0-9a-fA-F]+		{ sscanf(yytext,"%x",&yylval.val); return HEXNUMBER; }
add			{ return ADDOP; }
set			{ return SETOP; }
node(number|name)	{ return INFOOP; }
status|stat|s		{ return STATOP; }
config|conf		{ return SHOWOP; }
kill			{ return KILLOP; }
maxproc			{ return MAXPROC; }
smallpacketsize		{ return SMALLPACKETSIZE; }
resendtimeout		{ return RESENDTIMEOUT; }
debugmask		{ return DEBUGMASK; }
select			{ return SELECT; }
sleep			{ return SLEEP; }
no			{ return NO; }
nomaxproc		{ return NOMAXPROC; }
user			{ return USER; }
nouser			{ return NOUSER; }
node			{ return NODE; }
net			{ return NET; }
count			{ return COUNT; }
proc			{ return PROC; }
load			{ return LOAD; }
rdp			{ return RDP; }
rdpdebug		{ return RDPDEBUG; }
psiddebug		{ return PSIDDEBUG; }
nopsiddebug		{ return NOPSIDDEBUG; }
all|a|full|f		{ return ALL; }
restart			{ return RESTARTOP; }
shutdown		{ return SHUTDOWNOP; }
reset			{ return RESETOP; }
test			{ return TESTOP; }
quiet			{ return QUIET; }
verbose			{ return VERBOSE; }
normal			{ return NORMAL; }
exit|quit|e|q		{ return QUITOP; }
help|h|[?]		{ return HELPOP; }
version|v		{ return VERSIONOP; }
debug			{ return DEBUGOP;}
[-a-zA-Z0-9_.]+		{ strcpy(yylval.name,yytext); return NAME; }
[\n]			{ return NULLOP; }
.			{ printf("PSIadmin: unrecognized characters in input: %s \n",yytext); }
%%
