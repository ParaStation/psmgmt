%{
#include "y.tab.h"

extern int lineno;
extern unsigned char lasthname[];
void install(char *s);

#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char lexid[] __attribute__(( unused )) = "$Id: psid.scan.l,v 1.7 2002/02/15 19:35:25 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */
%}

%%
[ \t]+			;
#.*			{ return COMMENT; }
[0-9]+			{ sscanf(yytext,"%d",&yylval.val); return NUMBER; }
0x[0-9a-fA-F]+		{ sscanf(yytext,"%x",&yylval.val); return NUMBER; }

installdir		{ return INSTDIR; }
NrOfNodes		{ return NROFNODES; }
module			{ return MODULE; }
routingfile		{ return ROUTINGFILE; }

hwtype			{ return HWTYPE; }
myrinet			{ return MYRINET; }
ethernet		{ return ETHERNET; }
none			{ return NONE; }

licenseserver		{ return LICENSESERVER; }
licensekey		{ return LICENSEKEY; }

smallpacketsize		{ return SMALLPACKET; }
resendtimeout		{ return RESENDTIMEOUT; }
hnpend			{ return HNPEND; }
ackpend			{ return ACKPEND; }

mcastgroup|mcast        { return MCASTGROUP; }
mcastport               { return MCASTPORT; }
rdpport                 { return RDPPORT; }
psidselecttime		{ return PSIDSELECTTIME; }
declaredeadinterval	{ return DECLAREDEAD; }

rlimitdatasize		{ return RLIMITDATASIZE; }

sysloglevel		{ return SYSLOGLEVEL; }
syslog			{ return SYSLOG; }
log_daemon		{ return PSLOG_DAEMON; }
log_kern|kernel		{ return PSLOG_KERN; }
log_local0|local0	{ return PSLOG_LOCAL0; }
log_local1|local1	{ return PSLOG_LOCAL1; }
log_local2|local2	{ return PSLOG_LOCAL2; }
log_local3|local3	{ return PSLOG_LOCAL3; }
log_local4|local4	{ return PSLOG_LOCAL4; }
log_local5|local5	{ return PSLOG_LOCAL5; }
log_local6|local6	{ return PSLOG_LOCAL6; }
log_local7|local7	{ return PSLOG_LOCAL7; }

[\n]			{ lineno++; return NL; }
[-a-zA-Z0-9.]+		{ yylval.string=lasthname; install(yytext); return HOSTNAME; }
[-a-zA-Z0-9_./~#]+	{ yylval.string=lasthname; install(yytext); return FILENAME; }
[-a-zA-Z0-9_.:;<=?@\*%+]+	{ yylval.string=lasthname; install(yytext); return KEY; }

%%
