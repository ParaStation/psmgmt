%{
#include "y.tab.h"
#include <string.h>
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char lexid[] __attribute__(( unused )) = "$Id: admin.scan.l,v 1.6 2002/01/21 10:18:12 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */
%}

%option reject

%%
[ \t]+               { BEGIN INITIAL; }
[0-9]+               { sscanf(yytext,"%d",&yylval.val); return NUMBER; }
0x[0-9a-fA-F]+       { sscanf(yytext,"%x",&yylval.val); return HEXNUMBER; }

add                  { return ADDOP; }
set                  { return SETOP; }
status|stat|s        { return STATOP; }
config|conf          { return CONFIGOP; }
kill                 { return KILLOP; }
restart              { return RESTARTOP; }
shutdown             { return SHUTDOWNOP; }
reset                { return RESETOP; }
test                 { return TESTOP; }
exit|quit|e|q        { return QUITOP; }
help|h|[?]           { return HELPOP; }
version|v            { return VERSIONOP; }
[\n]                 { return NULLOP; }

smallpacketsize      { return SMALLPACKETSIZE; }
resendtimeout        { return RESENDTIMEOUT; }
debugmask            { return DEBUGMASK; }
rdpdebug             { return RDPDEBUG; }
psiddebug            { return PSIDDEBUG; }
nopsiddebug          { return NOPSIDDEBUG; }
selecttime           { return SELECTTIME; }

maxproc              { return MAXPROC; }
user                 { return USER; }
any                  { return ANY; }

node                 { return NODE; }
count|c|net          { return COUNT; }
rdp                  { return RDP; }
proc                 { return PROC; }
load                 { return LOAD; }
all|a                { return ALL; }

hardware|hw          { return HW; }

node(number|name)    { return NODEINFO; }

quiet                { return QUIET; }
normal               { return NORMAL; }
verbose              { return VERBOSE; }

[-a-zA-Z0-9_.]+      { strcpy(yylval.name,yytext); return NAME; }

.      { printf("PSIadmin: unrecognized characters in input: %s \n",yytext); }
%%
