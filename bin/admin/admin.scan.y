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
%token DEBUGOP
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
        | debugline
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

debugline:
	  DEBUGOP			{ printf("DEBUG what?\n"); }
	| DEBUGOP NAME			{ PSIADM_Debug($2,-1); }
	| DEBUGOP NAME NUMBER  	        { PSIADM_Debug($2,$3); }
	| DEBUGOP NAME HEXNUMBER        { PSIADM_Debug($2,$3); }
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


/*************************/
static void PrintHelp(void)
/*************************/
{
    printf("\n");
    printf("ParaStation Admin: available commands:\n");
    printf("======================================\n");
    printf("\n");
    printf("ADD:      Start ParaStation daemon process on one or all nodes\n");
    printf("KILL:     Terminate a ParaStation process on any node\n");
    printf("CONF:     Print current setting of internal parameters\n");
    printf("STATUS:   Status information [general|network|message-pool|processes]\n");
    printf("VERSION:  Print version numbers\n");
    printf("QUIT:     Quit PSIadmin\n");
    printf("\n");
    if(!getuid()){
	printf("Privileged commands:\n");
	printf("====================\n");
	printf("RESET:    Reset network or message-pool\n");
	printf("RESTART:  Restart ParaStation cluster\n");
	printf("SET:      Alter control parameters\n");
	printf("SHUTDOWN: Shutdown ParaStation cluster (all processes)\n");
	printf("TEST:     Test ParaStation network\n");
	printf("\n");
    }
    printf("For more information type HELP <command>\n");
    printf("\n");
    return;
}

/*****************************/
static void ParameterInfo(void)
/*****************************/
{
    printf("\n");
    printf("NODENAME:   Symbolic name of a machine in the ParaStation cluster\n");
    printf("NODENUMBER: Number of a machine in the ParaStation cluster\n");
    printf("            (0 <= number < %d)\n",PSI_getnrofnodes());
    printf("\n");
}

/****************************/
static void PrintAddHelp(void)
/****************************/
{
    printf("\n");
    printf("Add command:\n");
    printf("============\n");
    printf("\n");
    printf("SYNTAX:    ADD\n");
    printf("PARAMETER: NODENUMBER or NODENAME\n");
    ParameterInfo();
    printf("Description: Add starts the ParaStation daemon process (psid) on the given node\n");
    printf("             Normally this is done automatically when the system comes up.\n");
    printf("\n");
    return;
}

/****************************/
static void PrintSetHelp(void)
/****************************/
{
    printf("\n");
    printf("Set command (privileged):\n");
    printf("=========================\n");
    printf("\n");
    printf("SYNTAX:    SET\n");
    printf("PARAMETER: [NO]USER or [NO]MAXPROC or\n");
    printf("            SMALLPACKETSIZE or RESENDTIMEOUT or DEBUGMASK\n");
    printf("\n");
    printf("Description: SET USER USERNAME      grants access to a particular user\n");
    printf("             SET NOUSER             grants access to any users\n");
    printf("             SET MAXPROC NUMBER     set maximum ParaStation processes per node\n");
    printf("             SET NOMAXPROC          allow any number of ParaStation processes per node\n");
    printf("             SET RDPDEBUG VALUE [NODE] set verbose level for RDP protocol\n");
    printf("             SET PSIDDEBUG NODE     set verbose mode of psid on\n");
    printf("             SET NOPSIDDEBUG NODE   set verbose mode of psid off\n");
    printf("             SET SMALLPACKETSIZE SIZE  set the max size of PIO packets\n");
    printf("             SET RESENDTIMEOUT TIME  set retansmission timeout (in us)\n");
    printf("             SET DEBUGMASK NUMBER   set the local debugmask\n");
    printf("\n");
    return;
}

/*****************************/
static void PrintShowHelp(void)
/*****************************/
{
    printf("\n");
    printf("Config command:\n");
    printf("===============\n");
    printf("\n");
    printf("SYNTAX:    CONFIG\n");
    printf("\n");
    printf("Description: Print current setting of internal parameters\n");
    printf("\n");
    return;
}

/*****************************/
static void PrintStatHelp(void)
/*****************************/
{
    printf("\n");
    printf("Status command:\n");
    printf("===============\n");
    printf("\n");
    printf("SYNTAX:    STATUS\n");
    printf("PARAMETER: [NODE] or [COUNT RDP PROC ALL]\n");
    ParameterInfo();
    printf("Description: STATUS NODE   shows the active node(s) of a ParaStation cluster.\n");
    printf("             STATUS COUNT  shows the counters of active node(s) in a\n");
    printf("                         ParaStation cluster.\n");
    printf("             STATUS RDP    shows the status of the RDP protocol of active node(s)\n");
    printf("                         in a ParaStation cluster.\n");
    printf("             STATUS PROC   shows processes using ParaStation on active node(s)\n");
    printf("                         in a ParaStation cluster.\n");
    printf("             STATUS LOAD   shows load using ParaStation on active node(s)\n");
    printf("                         in a ParaStation cluster.\n");
    printf("             STATUS ALL    shows all statistics given above of all nodes in a\n");
    printf("                         ParaStation cluster\n");
    printf("\n");
    printf("For more information type HELP STATUS <subcommand>\n");
    printf("\n");
    return;
}

/*********************************/
static void PrintStatNodeHelp(void)
/*********************************/
{
    printf("\n");
    printf("Status node command:\n");
    printf("====================\n");
    printf("\n");
    printf("SYNTAX:    STATUS [NODE]\n");
    printf("PARAMETER: NODENUMBER or NODENAME\n");
    ParameterInfo();
    return;
}

/*********************************/
static void PrintPsidDebugHelp(void)
/*********************************/
{
    printf("\n");
    printf("Setting Debugging mode of the daemon:\n");
    printf("====================================\n");
    printf("\n");
    printf("SYNTAX:    SET PSIDDEBUG NODENUMER\n");
    printf("SYNTAX:    SET NOPSIDDEBUG NODENUMER\n");
    printf("\n");
    printf("       sets debugging verbose mode of the daemon on node NODENUMBER\n");
    printf("       to on or off.\n");
    printf("       The daemon logs a huge amount of message in the syslog.\n");
    printf("       Don't use PSIDDEBUG too long!!!!!\n");
    return;
}

static void PrintRdpDebugHelp(void)
{
    printf("\n");
    printf("Setting Debugging mode of RDP protocol:\n");
    printf("======================================\n");
    printf("\n");
    printf("SYNTAX:    SET RDPDEBUG LEVEL [NODENUMER]\n");
    printf("\n");
    printf("       sets debugging level of the RDP protocol to value LEVEL\n");
    printf("       If no NODENUMER is given, all nodes set their level.\n");
    printf("       For high value of LEVEL the daemon logs a huge amount \n");
    printf("       of message in the syslog.\n");
    printf("       Don't use a high level too long!!!!!\n");
    return;
}

static void PrintStatCountHelp(void)
{
    printf("\n");
    printf("Status count command:\n");
    printf("=====================\n");
    printf("\n");
    printf("SYNTAX:    STATUS COUNT\n");
    printf("PARAMETER: NODENUMBER or NODENAME\n");
    ParameterInfo();
    return;
}

static void PrintStatNetHelp(void)
{
    printf("\n");
    printf("Status net command:\n");
    printf("===================\n");
    printf("\n");
    printf("Command is obsolete, use 'status count'\n");
    printf("\n");
    printf("SYNTAX:    STATUS NET\n");
    printf("PARAMETER: NODENUMBER or NODENAME\n");
    ParameterInfo();
    return;
}

static void PrintStatRDPHelp(void)
{
    printf("\n");
    printf("Status RDP command:\n");
    printf("===================\n");
    printf("\n");
    printf("SYNTAX:    STATUS RDP\n");
    printf("PARAMETER: NODENUMBER or NODENAME\n");
    ParameterInfo();
    return;
}

static void PrintStatProcHelp(void)
{
    printf("\n");
    printf("Status proc command:\n");
    printf("====================\n");
    printf("\n");
    printf("SYNTAX:    STATUS PROC\n");
    printf("PARAMETER: NODENUMBER or NODENAME\n");
    ParameterInfo();
    return;
}

static void PrintResetHelp(void)
{
    printf("\n");
    printf("Reset command (privileged):\n");
    printf("===========================\n");
    printf("\n");
    printf("SYNTAX:    RESET [NET] [FROM [TO]]\n");
    printf("PARAMETER: NET\n");
    printf("           FROM: a integer number for the first node which should be reseted");
    printf("           TO  : a integer number for the last node which should be reseted");
    printf("\n");
    printf("Description: Resetting the network enforces all ParaStation"); 
    printf("             (or subrange [FROM,TO]) daemon processes\n");
    printf("             to put the interface boards in a known state. During a memory\n"); 
    printf("             reset the message-pool one each node is reorganized. As a \n"); 
    printf("             consequence ALL processes using the ParaStation cluster\n");
    printf("             are terminated (killed)!!!\n");
    printf("\n");
    return;
}

static void PrintRestartHelp(void)
{
    printf("\n");
    printf("Restart command (privileged):\n");
    printf("=============================\n");
    printf("\n");
    printf("SYNTAX: RESTART\n");
    printf("\n");
    printf("Description: All ParaStation daemon processes are forced to reinitialize\n");
    printf("             the ParaStation cluster. As a consequence ALL processes using\n");
    printf("             the ParaStation cluster are terminated (killed)!!!\n");
    printf("\n");
    return;
}

static void PrintShutdownHelp(void)
{
    printf("\n");
    printf("Shutdown command (privileged):\n");
    printf("=============================\n");
    printf("\n");
    printf("SYNTAX: SHUTDOWN\n");
    printf("\n");
    printf("Description: All ParaStation daemon processes within the ParaStation cluster\n");
    printf("             are forced to terminate. As a consequence ALL processes using\n");
    printf("             the ParaStation cluster are terminated (killed)!!!\n");
    printf("\n");
    return;
}

static void PrintKillHelp(void)
{
    printf("\n");
    printf("Kill command:\n");
    printf("=============\n");
    printf("\n");
    printf("SYNTAX: KILL NUMBER\n");
    printf("\n");
    printf("Description: Kills a process with the given task-id. The task-id can be\n");
    printf("             obtained from the STATUS PROC command.\n");
    printf("\n");
    return;
}

static void PrintTestHelp(void)
{
    printf("\n");
    printf("Test command (privileged):\n");
    printf("==========================\n");
    printf("\n");
    printf("SYNTAX: TEST \n");
    printf("PARAMETER: QUIET or NORMAL(default) or VERBOSE\n");
    printf("\n");
    printf("Description: All communications links in a ParaStation network are tested.\n");
    printf("             VERBOSE: each node is telling about his activities\n");
    printf("             NORMAL:  only the coordinator node is telling about his activities\n");
    printf("             QUIET:   just a ok is told on success\n");
    printf("\n");
    printf("\n");
    printf("\n");
    return;
}

static void PrintVersionHelp(void)
{
    printf("\n");
    printf("Version command:\n");
    printf("================\n");
    printf("\n");
    printf("SYNTAX: VERSION\n");
    printf("\n");
    printf("Description: Prints various version numbers\n");
    printf("\n");
    return;
}

static void PrintQuitHelp(void)
{
    printf("\n");
    printf("Quit command:\n");
    printf("=============\n");
    printf("\n");
    printf("SYNTAX: QUIT | EXIT \n");
    printf("\n");
    printf("Description: Exit the PSIadm shell\n");
    printf("\n");
    return;
}

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
