%{
#include <stdio.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>

#include "psi.h"
#include "psiadmin.h"

#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char yaccid[] __attribute__(( unused )) = "$Id: admin.scan.y,v 1.12 2002/02/15 19:25:00 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#define NODEERR -2

static int FirstNode, LastNode;
extern char * yytext;

static int CheckNr(int node);
static int CheckName(char *name);
static void CheckUserName(char *name);

%}

%union{
    int val;
    int none;
    char name[80];
}

%token <val> NUMBER HEXNUMBER
%token <name> NAME

%token ADDOP SETOP SHOWOP STATOP KILLOP CONFIGOP RESTARTOP SHUTDOWNOP RESETOP
%token TESTOP QUITOP HELPOP VERSIONOP NULLOP

%token SMALLPACKETSIZE RESENDTIMEOUT HNPEND ACKPEND SELECTTIME DEBUGMASK
%token RDPDEBUG RDPPKTLOSS RDPMAXRETRANS MCASTDEBUG PSIDDEBUG NOPSIDDEBUG

%token MAXPROC USER ANY

%token NODE COUNT RDP MCAST PROC LOAD ALL

%token HW

%token NODEINFO

%token VERBOSE NORMAL QUIET
%%

line:
          commline NULLOP     {return 0;}
        | NUMBER numberORname NULLOP
                {printf("PSIadmin: unknown command [%d]\n",$1); return 0;}
        | HEXNUMBER numberORname NULLOP
                {printf("PSIadmin: unknown command [%d]\n",$1); return 0;}
        | NAME numberORname NULLOP        
                {printf("PSIadmin: unknown command [%s]\n",$1); return 0;}
        | NULLOP              {return 0;}
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
        | showline
        | resetline
        | restartline
        | shutdownline
        | testline
        | helpline
        | killline
        | versionline
        | quitline
        ;

nodes:
                              {FirstNode=ALLNODES;LastNode=ALLNODES;}
        | NUMBER              {FirstNode=LastNode=CheckNr($1);}
        | HEXNUMBER           {FirstNode=LastNode=CheckNr($1);}
        | NAME                {FirstNode=LastNode=CheckName($1);}
        | NUMBER NUMBER       {FirstNode=CheckNr($1);LastNode=CheckNr($2);}
        | NUMBER HEXNUMBER    {FirstNode=CheckNr($1);LastNode=CheckNr($2);}
        | NUMBER NAME         {FirstNode=CheckNr($1);LastNode=CheckName($2);}
        | HEXNUMBER NUMBER    {FirstNode=CheckNr($1);LastNode=CheckNr($2);}
        | HEXNUMBER HEXNUMBER {FirstNode=CheckNr($1);LastNode=CheckNr($2);}
        | HEXNUMBER NAME      {FirstNode=CheckNr($1);LastNode=CheckName($2);}
        | NAME NUMBER         {FirstNode=CheckName($1);LastNode=CheckNr($2);}
        | NAME HEXNUMBER      {FirstNode=CheckName($1);LastNode=CheckNr($2);}
        | NAME NAME           {FirstNode=CheckName($1);LastNode=CheckName($2);}
        ;

addline: 
          ADDOP nodes         {MyAdd(FirstNode, LastNode);}
        ;

killline: 
          KILLOP              {printf("KILL needs a task-id as parameter\n");}
        | KILLOP NUMBER       {PSIADM_KillProc($2);}
        | KILLOP HEXNUMBER    {PSIADM_KillProc($2);}
        ;

setline:
          SETOP                        {printf("SET what?\n");}
        | SETOP MAXPROC
                {printf("SET MAXPROC needs number of processes\n");}
        | SETOP MAXPROC NUMBER         {PSIADM_SetMaxProc($3);}
        | SETOP MAXPROC HEXNUMBER      {PSIADM_SetMaxProc($3);}
        | SETOP MAXPROC ANY            {PSIADM_SetMaxProc(-1);}
        | SETOP USER                   {printf("SET USER needs username\n");}
        | SETOP USER NAME              {CheckUserName($3);}
        | SETOP USER ANY               {PSIADM_SetUser(-1);}
        | SETOP DEBUGMASK NUMBER       {PSIADM_SetDebugmask($3);}
        | SETOP DEBUGMASK HEXNUMBER    {PSIADM_SetDebugmask($3);}
        | SETOP RESENDTIMEOUT NUMBER   {PSIADM_SetResendTimeout($3);}
        | SETOP SMALLPACKETSIZE NUMBER {PSIADM_SetSmallPacketSize($3);}
        | SETOP HNPEND NUMBER          {PSIADM_SetHNPend($3);}
        | SETOP ACKPEND NUMBER         {PSIADM_SetAckPend($3);}
        | SETOP PSIDDEBUG nodes        {MySetPsidDebug(1,FirstNode,LastNode);}
        | SETOP NOPSIDDEBUG nodes
                {MySetPsidDebug(0,FirstNode,LastNode);}
        | SETOP SELECTTIME NUMBER nodes
                {MySetPsidSelectTime($3,FirstNode,LastNode);}
        | SETOP RDPDEBUG NUMBER nodes
                {MySetRDPDebug($3,FirstNode,LastNode);}
        | SETOP RDPPKTLOSS NUMBER nodes
                {MySetRDPPktLoss($3,FirstNode,LastNode);}
        | SETOP RDPMAXRETRANS NUMBER nodes
                {MySetRDPMaxRetrans($3,FirstNode,LastNode);}
        | SETOP MCASTDEBUG NUMBER nodes
                {MySetMCastDebug($3,FirstNode,LastNode);}
        ;

showline:
          SHOWOP                    {printf("SHOW what?\n");}
        | SHOWOP MAXPROC            {PSIADM_ShowMaxProc();}
        | SHOWOP USER               {PSIADM_ShowUser();}
        | SHOWOP DEBUGMASK          {PSIADM_ShowDebugmask();}
        | SHOWOP RESENDTIMEOUT      {PSIADM_ShowResendTimeout();}
        | SHOWOP SMALLPACKETSIZE    {PSIADM_ShowSmallPacketSize();}
        | SHOWOP HNPEND             {PSIADM_ShowHNPend();}
        | SHOWOP ACKPEND            {PSIADM_ShowAckPend();}
        | SHOWOP PSIDDEBUG nodes    {MyShowPsidDebug(FirstNode,LastNode);}
        | SHOWOP SELECTTIME nodes   {MyShowPsidSelectTime(FirstNode,LastNode);}
        | SHOWOP RDPDEBUG nodes     {MyShowRDPDebug(FirstNode,LastNode);}
        | SHOWOP RDPPKTLOSS nodes   {MyShowRDPPktLoss(FirstNode,LastNode);}
        | SHOWOP RDPMAXRETRANS nodes {MyShowRDPMaxRetrans(FirstNode,LastNode);}
        | SHOWOP MCASTDEBUG nodes   {MyShowMCastDebug(FirstNode,LastNode);}
        | CONFIGOP                  {PSIADM_ShowConfig();}
        ;

statline:
          STATOP ALL nodes    {MyNodeStat(FirstNode, LastNode);
                               MyCountStat(FirstNode, LastNode);
                               MyProcStat(FirstNode, LastNode);}
        | STATOP nodes        {MyNodeStat(FirstNode, LastNode);}
        | STATOP NODE nodes   {MyNodeStat(FirstNode, LastNode);}
        | STATOP COUNT nodes  {MyCountStat(FirstNode, LastNode);}
        | STATOP PROC nodes   {MyProcStat(FirstNode, LastNode);}
        | STATOP LOAD nodes   {MyLoadStat(FirstNode, LastNode);}
        | STATOP RDP nodes    {MyRDPStat(FirstNode, LastNode);}
        | STATOP MCAST nodes  {MyMCastStat(FirstNode, LastNode);}
        ;

resetline:
          RESETOP nodes       {PSIADM_Reset(0,FirstNode,LastNode);}
        | RESETOP HW nodes    {PSIADM_Reset(1,FirstNode,LastNode);}
        ;

restartline:
          RESTARTOP nodes     {PSIADM_Reset(1,FirstNode,LastNode);}
        ;

shutdownline:
          SHUTDOWNOP nodes    {PSIADM_ShutdownCluster(FirstNode,LastNode);}
        ;

testline:
          TESTOP              {PSIADM_TestNetwork(1);}
        | TESTOP VERBOSE      {PSIADM_TestNetwork(2);}
        | TESTOP QUIET        {PSIADM_TestNetwork(0);}
        | TESTOP NORMAL       {PSIADM_TestNetwork(1);}
        ;

helpline:
          HELPOP nodes                 {PrintHelp();}
        | HELPOP HELPOP                {PrintHelp();}
        | HELPOP NODEINFO              {NodeInfo();}

        | HELPOP ADDOP                 {PrintAddHelp();}
        | HELPOP ADDOP NODEINFO        {PrintAddHelp();}

        | HELPOP STATOP nodes          {PrintStatHelp();}
        | HELPOP STATOP NODEINFO       {PrintStatHelp();}
        | HELPOP STATOP NODE nodes     {PrintStatNodeHelp();}
        | HELPOP STATOP COUNT nodes    {PrintStatCountHelp();}
        | HELPOP STATOP RDP nodes      {PrintStatRDPHelp();}
        | HELPOP STATOP MCAST nodes    {PrintStatMCastHelp();}
        | HELPOP STATOP PROC nodes     {PrintStatProcHelp();}
        | HELPOP STATOP ALL nodes      {PrintStatNodeHelp();
                                        PrintStatCountHelp();
                                        PrintStatProcHelp();}

        | HELPOP RESETOP nodes         {PrintResetHelp();}
        | HELPOP RESETOP HW nodes      {PrintResetHelp();}
        | HELPOP RESTARTOP nodes       {PrintRestartHelp();}
        | HELPOP SHUTDOWNOP nodes      {PrintShutdownHelp();}

        | HELPOP KILLOP                {PrintKillHelp();}

        | HELPOP VERSIONOP             {PrintVersionHelp();}

        | HELPOP QUITOP                {PrintQuitHelp();}

        | HELPOP CONFIGOP              {PrintConfigHelp();}

        | HELPOP SETOP                 {PrintSetHelp();}
        | HELPOP SETOP MAXPROC         {PrintSetHelp();}
        | HELPOP SETOP USER            {PrintSetHelp();}
        | HELPOP SETOP SMALLPACKETSIZE {PrintSetHelp();}
        | HELPOP SETOP RESENDTIMEOUT   {PrintSetHelp();}
        | HELPOP SETOP DEBUGMASK       {PrintSetHelp();}
        | HELPOP SETOP PSIDDEBUG       {PrintPsidDebugHelp();}
        | HELPOP SETOP NOPSIDDEBUG     {PrintPsidDebugHelp();}
        | HELPOP SETOP RDPDEBUG        {PrintRDPDebugHelp();}
        | HELPOP SETOP RDPPKTLOSS      {PrintRDPPktLossHelp();}
        | HELPOP SETOP MCASTDEBUG      {PrintMCastDebugHelp();}

        | HELPOP TESTOP                {PrintTestHelp();}
        | HELPOP TESTOP NORMAL         {PrintTestHelp();}
        | HELPOP TESTOP QUIET          {PrintTestHelp();}
        | HELPOP TESTOP VERBOSE        {PrintTestHelp();}
        ;

versionline:
          VERSIONOP           {PSIADM_Version();}
        ;

quitline:
          QUITOP              {PSIADM_Exit();}
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

static int CheckNr(int node)
{
    register int NrOfNodes = PSI_getnrofnodes();

    if ((node<0) || (node>NrOfNodes-1)){
	printf("PSIadmin: Illegal nodenumber %d\n",node);
	return NODEERR;
    }
    return node;
}

static int CheckName(char *name)
{
    register int node = PSIADM_LookUpNodeName(name); 

    if (node==-1){
	printf("PSIadmin: Illegal nodename %s\n",name);
	return NODEERR;
    }

    return node;
}

static void MyAdd(int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_AddNode(first, last);

    return;
}

static void MyNodeStat(int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_NodeStat(first, last);

    return;
}

static void MyCountStat(int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_CountStat(first, last);

    return;
}

static void MyProcStat(int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_ProcStat(first, last);

    return;
}

static void MyLoadStat(int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_LoadStat(first, last);

    return;
}

static void MyRDPStat(int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_RDPStat(first, last);

    return;
}

static void MyMCastStat(int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_MCastStat(first, last);

    return;
}

static void MyReset(int what, int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_Reset(what, first, last);
}

static void MySetPsidDebug(int what, int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_SetPsidDebug(what, first, last);
}

static void MyShowPsidDebug(int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_ShowPsidDebug(first, last);
}

static void MySetPsidSelectTime(int what, int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_SetPsidSelectTime(what, first, last);
}

static void MyShowPsidSelectTime(int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_ShowPsidSelectTime(first, last);
}

static void MySetRDPDebug(int what, int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_SetRDPDebug(what, first, last);
}

static void MyShowRDPDebug(int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_ShowRDPDebug(first, last);
}

static void MySetRDPPktLoss(int what, int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_SetRDPPktLoss(what, first, last);
}

static void MyShowRDPPktLoss(int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_ShowRDPPktLoss(first, last);
}

static void MySetRDPMaxRetrans(int what, int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_SetRDPMaxRetrans(what, first, last);
}

static void MyShowRDPMaxRetrans(int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_ShowRDPMaxRetrans(first, last);
}

static void MySetMCastDebug(int what, int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_SetMCastDebug(what, first, last);
}

static void MyShowMCastDebug(int first, int last)
{
    if ( (first != NODEERR) && (last != NODEERR))
	PSIADM_ShowMCastDebug(first, last);
}
