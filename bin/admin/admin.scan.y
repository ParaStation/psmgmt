%{
#include <stdio.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>

#include "psi.h"
#include "psiadmin.h"

static int NodeNr;
extern char * yytext;

static int CheckNodeNr(int node);
static int GetNrFromName(char *name);
static void CheckUserName(char *name);

%}

%union{
    int val;
    int none;
    char name[80];
}

%token <val> NUMBER HEXNUMBER
%token <name> NAME
%token NODE NET COUNT PROC LOAD RDP ALL NOMAXPROC MAXPROC USER NOUSER
%token SMALLPACKETSIZE SELECT SLEEP NO
%token RESENDTIMEOUT DEBUGMASK
%token ADDOP STATOP RESTARTOP RESETOP TESTOP QUITOP HELPOP NULLOP INFOOP SETOP 
%token SHUTDOWNOP VERSIONOP KILLOP SHOWOP
%token VERBOSE NORMAL QUIET RDPDEBUG PSIDDEBUG NOPSIDDEBUG
%%

line:
	  commline NULLOP	{ return 0; }
	| NUMBER numberORname NULLOP
		{ printf("PSIadmin: unknown command [%d]\n",$1); return 0; }
	| HEXNUMBER numberORname NULLOP
		{ printf("PSIadmin: unknown command [%d]\n",$1); return 0; }
	| NAME numberORname NULLOP	
		{ printf("PSIadmin: unknown command [%s]\n",$1); return 0; }
	| NULLOP		{ return 0; }
	;

numberORname:
	  /* empty */
	| numberORname NAME
	| numberORname NUMBER
	| numberORname HEXNUMBER
	;

commline:
	  addline
	| statline
	| setline
	| resetline
	| restartline
	| shutdownline
	| testline
	| helpline
	| killline
	| versionline
	| showline
	| quitline
	;

addline: 
	  ADDOP 			{ NodeNr=ALLNODES; MyAdd(ALLNODES); }
	| ADDOP NUMBER			{ MyAdd(CheckNodeNr($2)); }
	| ADDOP HEXNUMBER		{ MyAdd(CheckNodeNr($2)); }
	| ADDOP NAME			{ MyAdd(GetNrFromName($2)); }
	;

killline: 
	  KILLOP 			{ printf("KILL needs task-id as second parameter\n"); }
	| KILLOP NUMBER			{ PSIADM_KillProc($2); }
	| KILLOP HEXNUMBER		{ PSIADM_KillProc($2); }
	;

setline:
	  SETOP				{ printf("SET what?\n"); }
	| SETOP MAXPROC			{ printf("SET MAXPROC needs number of processes\n"); }
	| SETOP MAXPROC NUMBER  	{ PSIADM_SetMaxProc($3); }
	| SETOP MAXPROC HEXNUMBER	{ PSIADM_SetMaxProc($3); }
	| SETOP NOMAXPROC		{ PSIADM_SetMaxProc(-1); }
	| SETOP USER			{ printf("SET USER needs username\n"); }
	| SETOP USER NAME		{ CheckUserName($3); }
	| SETOP NOUSER			{ PSIADM_SetUser(-1); }
	| SETOP DEBUGMASK NUMBER	{ PSIADM_SetDebugmask($3); }
	| SETOP DEBUGMASK HEXNUMBER	{ PSIADM_SetDebugmask($3); }
	| SETOP RESENDTIMEOUT NUMBER	{ PSIADM_SetResendTimeout($3); }
	| SETOP SMALLPACKETSIZE NUMBER	{ PSIADM_SetSmallPacketSize($3); }
	| SETOP PSIDDEBUG 	        { printf("SET PSIDEBUG needs node number\n"); }
	| SETOP PSIDDEBUG NUMBER	{ PSIADM_SetPsidDebug(1,$3); }
	| SETOP NOPSIDDEBUG NUMBER	{ PSIADM_SetPsidDebug(0,$3); }
        | SETOP RDPDEBUG NUMBER 	{ PSIADM_SetRdpDebug($3,-1); }
        | SETOP RDPDEBUG NUMBER NUMBER 	{ PSIADM_SetRdpDebug($3,$4); }
	;

statline:
	  STATOP 			{ NodeNr=ALLNODES; MyNodeStat(NodeNr); }
	| STATOP ALL  			{ NodeNr=ALLNODES; MyNodeStat(NodeNr);
					  MyCountStat(NodeNr);
					  MyProcStat(NodeNr); }
	| STATOP NODE			{ NodeNr=ALLNODES; MyNodeStat(NodeNr); }
	| STATOP NUMBER			{ MyNodeStat(CheckNodeNr($2)); }
	| STATOP HEXNUMBER		{ MyNodeStat(CheckNodeNr($2)); }
	| STATOP NAME			{ MyNodeStat(GetNrFromName($2)); }
	| STATOP NODE NUMBER		{ MyNodeStat(CheckNodeNr($3)); }
	| STATOP NODE HEXNUMBER		{ MyNodeStat(CheckNodeNr($3)); }
	| STATOP NODE NAME		{ MyNodeStat(GetNrFromName($3)); }
	| STATOP NET			{ NodeNr=ALLNODES; MyCountStat(NodeNr); }
	| STATOP NET NUMBER		{ MyCountStat(CheckNodeNr($3)); }
	| STATOP NET HEXNUMBER		{ MyCountStat(CheckNodeNr($3)); }
	| STATOP NET NAME		{ MyCountStat(GetNrFromName($3)); }
	| STATOP COUNT			{ NodeNr=ALLNODES; MyCountStat(NodeNr); }
	| STATOP COUNT NUMBER		{ MyCountStat(CheckNodeNr($3)); }
	| STATOP COUNT HEXNUMBER	{ MyCountStat(CheckNodeNr($3)); }
	| STATOP COUNT NAME		{ MyCountStat(GetNrFromName($3)); }
	| STATOP PROC			{ NodeNr=ALLNODES; MyProcStat(NodeNr); }
	| STATOP PROC NUMBER		{ MyProcStat(CheckNodeNr($3)); }
	| STATOP PROC HEXNUMBER		{ MyProcStat(CheckNodeNr($3)); }
	| STATOP PROC NAME		{ MyProcStat(GetNrFromName($3)); }
	| STATOP LOAD			{ NodeNr=ALLNODES; MyLoadStat(NodeNr); }
	| STATOP LOAD NUMBER		{ MyLoadStat(CheckNodeNr($3)); }
	| STATOP LOAD HEXNUMBER		{ MyLoadStat(CheckNodeNr($3)); }
	| STATOP LOAD NAME		{ MyLoadStat(GetNrFromName($3)); }
	| STATOP RDP			{ NodeNr=ALLNODES; MyRDPStat(NodeNr); }
	| STATOP RDP NUMBER		{ MyRDPStat(CheckNodeNr($3)); }
	| STATOP RDP HEXNUMBER		{ MyRDPStat(CheckNodeNr($3)); }
	| STATOP RDP NAME		{ MyRDPStat(GetNrFromName($3)); }
	;

resetline:
	  RESETOP			{ PSIADM_Reset(3,0,-1); }
	| RESETOP NUMBER		{ PSIADM_Reset(3,$2,$2); }
	| RESETOP NUMBER NUMBER		{ PSIADM_Reset(3,$2,$3); }
	| RESETOP NET			{ PSIADM_Reset(1,0,-1); }
	| RESETOP NET NUMBER		{ PSIADM_Reset(1,$3,$3); }
	| RESETOP NET NUMBER NUMBER	{ PSIADM_Reset(1,$3,$4); }
	;

restartline:
	  RESTARTOP			{ PSIADM_Reset(3,0,-1); }
	;

shutdownline:
	  SHUTDOWNOP			{ PSIADM_ShutdownCluster(-1,-1); }
	| SHUTDOWNOP NUMBER		{ PSIADM_ShutdownCluster($2,$2); }
	| SHUTDOWNOP NUMBER NUMBER	{ PSIADM_ShutdownCluster($2,$3); }
	;

showline:
	  SHOWOP			{ PSIADM_ShowParameter(); }
	;

testline:
	  TESTOP			{ PSIADM_TestNetwork(1); }
	| TESTOP VERBOSE		{ PSIADM_TestNetwork(2); }
	| TESTOP QUIET			{ PSIADM_TestNetwork(0); }
	| TESTOP NORMAL			{ PSIADM_TestNetwork(1); }
	;

helpline:
	  HELPOP 			{ PrintHelp(); }
	| HELPOP HELPOP			{ PrintHelp(); }
	| HELPOP NAME			{ PrintHelp(); }
	| HELPOP NUMBER			{ PrintHelp(); }
	| HELPOP HEXNUMBER		{ PrintHelp(); }
	| HELPOP INFOOP			{ ParameterInfo(); }
	| HELPOP ADDOP			{ PrintAddHelp(); }
	| HELPOP ADDOP INFOOP		{ PrintAddHelp(); }
	| HELPOP STATOP			{ PrintStatHelp(); }
	| HELPOP STATOP INFOOP		{ PrintStatHelp(); }
	| HELPOP STATOP NODE		{ PrintStatNodeHelp(); }
	| HELPOP STATOP	COUNT		{ PrintStatCountHelp(); }
	| HELPOP STATOP	NET		{ PrintStatNetHelp(); }
	| HELPOP STATOP RDP		{ PrintStatRDPHelp(); }
	| HELPOP STATOP PROC		{ PrintStatProcHelp(); }
	| HELPOP STATOP ALL		{ PrintStatNodeHelp();
	                                  PrintStatCountHelp();
                                          PrintStatProcHelp(); }
	| HELPOP STATOP NUMBER		{ PrintStatHelp(); }
	| HELPOP STATOP HEXNUMBER	{ PrintStatHelp(); }
	| HELPOP STATOP NAME		{ PrintStatHelp(); }
	| HELPOP RESETOP		{ PrintResetHelp(); }
	| HELPOP RESETOP NET 		{ PrintResetHelp(); }
	| HELPOP RESTARTOP 		{ PrintRestartHelp(); }
	| HELPOP SHUTDOWNOP 		{ PrintShutdownHelp(); }
	| HELPOP TESTOP 		{ PrintTestHelp(); }
	| HELPOP TESTOP NORMAL 		{ PrintTestHelp(); }
	| HELPOP TESTOP QUIET		{ PrintTestHelp(); }
	| HELPOP TESTOP VERBOSE		{ PrintTestHelp(); }
	| HELPOP KILLOP 		{ PrintKillHelp(); }
	| HELPOP VERSIONOP 		{ PrintVersionHelp(); }
	| HELPOP QUITOP 		{ PrintQuitHelp(); }
	| HELPOP SHOWOP			{ PrintShowHelp(); }
	| HELPOP SETOP			{ PrintSetHelp(); }
	| HELPOP SETOP MAXPROC		{ PrintSetHelp(); }
	| HELPOP SETOP NOMAXPROC	{ PrintSetHelp(); }
	| HELPOP SETOP USER		{ PrintSetHelp(); }
	| HELPOP SETOP NOUSER		{ PrintSetHelp(); }
	| HELPOP SETOP SMALLPACKETSIZE	{ PrintSetHelp(); }
	| HELPOP SETOP RESENDTIMEOUT	{ PrintSetHelp(); }
	| HELPOP SETOP DEBUGMASK	{ PrintSetHelp(); }
	| HELPOP SETOP PSIDDEBUG	{ PrintPsidDebugHelp(); }
	| HELPOP SETOP NOPSIDDEBUG	{ PrintPsidDebugHelp(); }
	| HELPOP SETOP RDPDEBUG   	{ PrintRdpDebugHelp(); }
	;

versionline:
	  VERSIONOP 			{ PSIADM_Version(); }
	;

quitline:
	  QUITOP 			{ PSIADM_Exit(); }
	;

%%

#include "psiadmin_help.c"

int yywrap(void)
{
    return 0;
}

void yyerror(char *s)
{
    printf("PSIadmin: %s\n",s);
    return;
}

static void CheckUserName(char *name)
{
    struct passwd *passwd;

    if ((passwd = getpwnam(name))==NULL){
	printf("PSIamnin: Unknown user %s\n",name);
	return;
    };
    PSIADM_SetUser(passwd->pw_uid);
    return;
}

static int CheckNodeNr(int node)
{
    register int NrOfNodes = PSI_getnrofnodes();

    if ((node<0) || (node>NrOfNodes-1)){
	printf("PSIadmin: Illegal nodenumber %d\n",node);
	return NODEERR;
    }
    return node;
}

static int GetNrFromName(char *name)
{
    register int node = PSIADM_LookUpNodeName(name); 

    if (node==-1){
	printf("PSIadmin: Illegal nodename %s\n",name);
	return NODEERR;
    }

    return node;
}

static void MyAdd(int node){if(node!=NODEERR){PSIADM_AddNode(node);}return;}
static void MyNodeStat(int node){if(node!=NODEERR){PSIADM_NodeStat(node);}return;}
static void MyCountStat(int node){if(node!=NODEERR){PSIADM_CountStat(node);}return;}
static void MyProcStat(int node){if(node!=NODEERR){PSIADM_ProcStat(node);}return;}
static void MyLoadStat(int node){if(node!=NODEERR){PSIADM_LoadStat(node);}return;}
static void MyRDPStat(int node){if(node!=NODEERR){PSIADM_RDPStat(node);}return;}
