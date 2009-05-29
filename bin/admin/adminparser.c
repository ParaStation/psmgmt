/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>

#include "pscommon.h"
#include "pstask.h"
#include "psprotocol.h"
#include "psi.h"
#include "psiinfo.h"
#include "parser.h"


#include "commands.h"

#include "adminparser.h"

#include "helpmsgs.c"

static char parserversion[] = "$Revision$";

static char *defaultNL = NULL;

static int setupDefaultNL(void)
{
    defaultNL = malloc(PSC_getNrOfNodes());
    if (!defaultNL) return 0;

    memset(defaultNL, 1, PSC_getNrOfNodes());
    return 1;
}

static char *getNodeList(char *nl_descr)
{
    static char *nl = NULL;

    if (!strcasecmp(nl_descr, "all")) {
	nl = realloc(nl, PSC_getNrOfNodes());
	memset(nl, 1, PSC_getNrOfNodes());

	return nl;
    } else {
	char *tmp = strdup(nl_descr);
	char *ret = PSC_parseNodelist(tmp);

	free(tmp);
	if (ret) return ret;
    }

    {
	PSnodes_ID_t node;
	struct hostent *hp = gethostbyname(nl_descr);
	struct sockaddr_in sa;
	int err;

	if (!hp) goto error;

	memcpy(&sa.sin_addr, *hp->h_addr_list, sizeof(sa.sin_addr));
	err = PSI_infoNodeID(-1, PSP_INFO_HOST, &sa.sin_addr.s_addr, &node, 1);

	if (err || node==-1) goto error;

	nl = realloc(nl, PSC_getNrOfNodes());
	memset(nl, 0, PSC_getNrOfNodes());

	nl[node] = 1;
	return nl;
    }

 error:
    printf("Illegal nodename '%s'\n", nl_descr);
    return NULL;
}

static int addCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) return -1;
    }

    PSIADM_AddNode(nl);
    return 0;

 error:
    printError(&addInfo);
    return -1;
}

static int shutdownCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) return -1;
    }

    PSIADM_ShutdownNode(nl);
    return 0;

 error:
    printError(&shutdownInfo);
    return -1;
}

static int hwstartCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL, *hw = NULL;
    int hwIndex = -1;

    if (nl_descr && !strcasecmp(nl_descr, "hw")) {
	hw = parser_getString();
	nl_descr = parser_getString();
    }

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) return -1;
    }

    if (hw) {
	int err = PSI_infoInt(-1, PSP_INFO_HWINDEX, hw, &hwIndex, 1);
	if (err || (hwIndex == -1 && strcasecmp(hw, "all"))) goto error;
    }
    PSIADM_HWStart(hwIndex, nl);
    return 0;

 error:
    printError(&hwstartInfo);
    return -1;
}

static int hwstopCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL, *hw = NULL;
    int hwIndex = -1;

    if (nl_descr && !strcasecmp(nl_descr, "hw")) {
	hw = parser_getString();
	nl_descr = parser_getString();
    }

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) return -1;
    }

    if (hw) {
	int err = PSI_infoInt(-1, PSP_INFO_HWINDEX, hw, &hwIndex, 1);
	if (err || (hwIndex == -1 && strcasecmp(hw, "all"))) goto error;
    }
    PSIADM_HWStop(hwIndex, nl);
    return 0;

 error:
    printError(&hwstopInfo);
    return -1;
}

static int restartCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) return -1;
    }

    PSIADM_Reset(1, nl);
    return 0;

 error:
    printError(&restartInfo);
    return -1;
}

static int rangeCommand(char *token)
{
    char *nl_descr = parser_getString();

    if (parser_getString()) goto error;

    if (nl_descr) {
	char *nl = getNodeList(nl_descr);
	if (!nl) return -1;

	memcpy(defaultNL, nl, PSC_getNrOfNodes());
    } else {
	printf(" ");
	PSC_printNodelist(defaultNL);
	printf("\n");
    }

    return 0;

 error:
    printError(&rangeInfo);
    return -1;
}

static int resetCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;
    int hw = 0;

    if (nl_descr && !strcasecmp(nl_descr, "hw")) {
	nl_descr = parser_getString();
	hw = 1;
    }

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) return -1;
    }

    PSIADM_Reset(hw, nl);
    return 0;

 error:
    printError(&resetInfo);
    return -1;
}

/**************************** list commands *****************************/
static int listCountCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;
    int hwIndex = -1;

    if (nl_descr && !strcasecmp(nl_descr, "hw")) {
	char *hw = parser_getString();
	if (hw) {
	    int err = PSI_infoInt(-1, PSP_INFO_HWINDEX, hw, &hwIndex, 1);
	    if (err || hwIndex == -1) goto error;
	} else goto error;
	nl_descr = parser_getString();
    }

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_CountStat(hwIndex, nl);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listProcCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;
    long cnt = -1;

    if (nl_descr && !strcasecmp(nl_descr, "cnt")) {
	char *tok = parser_getString();
	if (!tok) goto error;
	if (parser_getNumber(tok, &cnt) || cnt < 0) goto error;
	nl_descr = parser_getString();
    }

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    if (!strcasecmp(token, "processes") || !strcasecmp(token, "p")) {
	PSIADM_ProcStat(cnt, 0, nl);
    } else if (!strcasecmp(token, "aproc")
	       || !strcasecmp(token, "allprocesses")) {
	PSIADM_ProcStat(cnt, 1, nl);
    } else goto error;
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listPluginCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_PluginStat(nl);

    return 0;

 error:
    printError(&pluginInfo);
    return -1;
}

static int listNodeCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_NodeStat(nl);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listSummaryCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_SummaryStat(nl);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listRDPCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_RDPStat(nl);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listMCastCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_MCastStat(nl);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listLoadCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_LoadStat(nl);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listMemoryCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_MemStat(nl);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listHWCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_HWStat(nl);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listJobsCommand(char *token)
{
    char *tid_descr = parser_getString();
    PStask_ID_t task = 0;
    PSpart_list_t opt = 0;

    while (tid_descr) {
	if (!strcasecmp(tid_descr, "state")) {
	    char *tok = parser_getString();
	    if (!tok) goto error;
	    if (!strncasecmp(tok, "p", 1)) {
		opt |= PART_LIST_PEND;
	    } else if (!strncasecmp(tok, "s", 1)) {
		opt |= PART_LIST_SUSP;
	    } else if (!strncasecmp(tok, "r", 1)) {
		opt |= PART_LIST_RUN;
	    } else goto error;
	    tid_descr = parser_getString();
	    continue;
	} else if (!strncasecmp(tid_descr, "slots", 1)) {
	    opt |= PART_LIST_NODES;
	    tid_descr = parser_getString();
	    continue;
	} else break;
    }

    if (! (opt & ~PART_LIST_NODES)) {
	opt |= PART_LIST_PEND | PART_LIST_SUSP | PART_LIST_RUN;
    }

    if (tid_descr) {
	long tmp;
	if (parser_getNumber(tid_descr, &tmp)) goto error;
	task = tmp;
    }
    if (task && (PSC_getID(task) < 0 || PSC_getID(task) > PSC_getNrOfNodes()
		 || PSC_getPID(task) < 1)) goto error; /* task out of range */
    if (parser_getString()) goto error; /* trailing garbage */

    PSIADM_JobStat(task, opt);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listVersionCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_VersionStat(nl);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listSpecialCommand(char *token)
{
    char *nl = defaultNL;

    if (parser_getString()) goto error;

    /* Maybe this is like 'list a-b' */
    if (token) {
	nl = getNodeList(token);
	if (!nl) return -1;
    }

    PSIADM_NodeStat(nl);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static keylist_t listList[] = {
    {"aproc", listProcCommand},
    {"allprocesses", listProcCommand},
    {"count", listCountCommand},
    {"hardware", listHWCommand},
    {"hw", listHWCommand},
    {"jobs", listJobsCommand},
    {"load", listLoadCommand},
    {"mcast", listMCastCommand},
    {"memory", listMemoryCommand},
    {"node", listNodeCommand},
    {"processes", listProcCommand},
    {"p", listProcCommand},
    {"plugins", listPluginCommand},
    {"rdp", listRDPCommand},
    {"summary", listSummaryCommand},
    {"versions", listVersionCommand},
    {NULL, listSpecialCommand}
};
static parser_t listParser = {" \t\n", listList};

static int listCommand(char *token)
{
    char *what = parser_getString();

    if (!what) listSpecialCommand(what);

    return parser_parseString(what, &listParser);
}

/************************* set / show commands **************************/

static long procsFromString(char *procStr)
{
    long procs;

    if (strcasecmp(procStr, "any") == 0) return -1;
    if (!parser_getNumber(procStr, &procs) && procs > -1) return procs;

    printf("Unknown value '%s'\n", procStr);
    return -2;
}

static uid_t uidFromString(char *user)
{
    long uid;
    struct passwd *passwd = getpwnam(user);

    if (!user) return -2;
    if (strcasecmp(user, "any") == 0) return -1;
    if (!parser_getNumber(user, &uid) && uid > -1) return uid;
    if (passwd) return passwd->pw_uid;

    printf("Unknown user '%s'\n", user);
    return -2;
}

static gid_t gidFromString(char *group)
{
    long gid;
    struct group *grp = getgrnam(group);

    if (!group) return -2;
    if (strcasecmp(group, "any") == 0) return -1;
    if (!parser_getNumber(group, &gid) && gid > -1) return gid;
    if (grp) return grp->gr_gid;

    printf("Unknown group '%s'\n", group);
    return -2;
}

static PSP_Option_t setShowOpt = PSP_OP_UNKNOWN;

static int setShowMaxProc(char *token)
{
    setShowOpt = PSP_OP_PROCLIMIT;
    return 0;
}

static int setShowUser(char *token)
{
    setShowOpt = PSP_OP_UID;
    return 0;
}

static int setShowGroup(char *token)
{
    setShowOpt = PSP_OP_GID;
    return 0;
}

static int setShowAdminUser(char *token)
{
    setShowOpt = PSP_OP_ADMUID;
    return 0;
}

static int setShowAdminGroup(char *token)
{
    setShowOpt = PSP_OP_ADMGID;
    return 0;
}

static int setShowPSIDDebug(char *token)
{
    setShowOpt = PSP_OP_PSIDDEBUG;
    return 0;
}

static int setShowSelectTime(char *token)
{
    setShowOpt = PSP_OP_PSIDSELECTTIME;
    return 0;
}

static int setShowStatusTimeout(char *token)
{
    setShowOpt = PSP_OP_STATUS_TMOUT;
    return 0;
}

static int setShowDeadLimit(char *token)
{
    setShowOpt = PSP_OP_STATUS_DEADLMT;
    return 0;
}

static int setShowStatBCast(char *token)
{
    setShowOpt = PSP_OP_STATUS_BCASTS;
    return 0;
}

static int setShowRDPDebug(char *token)
{
    setShowOpt = PSP_OP_RDPDEBUG;
    return 0;
}

static int setShowRDPTmOut(char *token)
{
    setShowOpt = PSP_OP_RDPTMOUT;
    return 0;
}

static int setShowRDPPktLoss(char *token)
{
    setShowOpt = PSP_OP_RDPPKTLOSS;
    return 0;
}

static int setShowRDPMaxRetrans(char *token)
{
    setShowOpt = PSP_OP_RDPMAXRETRANS;
    return 0;
}

static int setShowRDPMaxACKPend(char *token)
{
    setShowOpt = PSP_OP_RDPMAXACKPEND;
    return 0;
}

static int setShowRDPRsndTmOut(char *token)
{
    setShowOpt = PSP_OP_RDPRSNDTMOUT;
    return 0;
}

static int setShowRDPClsdTmOut(char *token)
{
    setShowOpt = PSP_OP_RDPCLSDTMOUT;
    return 0;
}

static int setShowRDPRetrans(char *token)
{
    setShowOpt = PSP_OP_RDPRETRANS;
    return 0;
}

static int setShowMaster(char *token)
{
    setShowOpt = PSP_OP_MASTER;
    return 0;
}

static int setShowMCastDebug(char *token)
{
    setShowOpt = PSP_OP_MCASTDEBUG;
    return 0;
}

static int setShowFOS(char *token)
{
    setShowOpt = PSP_OP_FREEONSUSP;
    return 0;
}

static int setShowHOB(char *token)
{
    setShowOpt = PSP_OP_HANDLEOLD;
    return 0;
}

static int setShowNodesSort(char *token)
{
    setShowOpt = PSP_OP_NODESSORT;
    return 0;
}

static int setShowOverbook(char *token)
{
    setShowOpt = PSP_OP_OVERBOOK;
    return 0;
}

static int setShowExclusive(char *token)
{
    setShowOpt = PSP_OP_EXCLUSIVE;
    return 0;
}

static int setShowRunJobs(char *token)
{
    setShowOpt = PSP_OP_RUNJOBS;
    return 0;
}

static int setShowStarter(char *token)
{
    setShowOpt = PSP_OP_STARTER;
    return 0;
}

static int setShowPinProcs(char *token)
{
    setShowOpt = PSP_OP_PINPROCS;
    return 0;
}

static int setShowBindMem(char *token)
{
    setShowOpt = PSP_OP_BINDMEM;
    return 0;
}

static int setShowCPUMap(char *token)
{
    setShowOpt = PSP_OP_CPUMAP;
    return 0;
}

static int setShowAccounter(char *token)
{
    setShowOpt = PSP_OP_ACCT;
    return 0;
}

static int setShowAcctPoll(char *token)
{
    setShowOpt = PSP_OP_ACCTPOLL;
    return 0;
}

static int setShowSupplGrps(char *token)
{
    setShowOpt = PSP_OP_SUPPL_GRPS;
    return 0;
}

static int setShowRL_AS(char *token)
{
    setShowOpt = PSP_OP_RL_AS;
    return 0;
}

static int setShowRL_Core(char *token)
{
    setShowOpt = PSP_OP_RL_CORE;
    return 0;
}

static int setShowRL_CPU(char *token)
{
    setShowOpt = PSP_OP_RL_CPU;
    return 0;
}

static int setShowRL_Data(char *token)
{
    setShowOpt = PSP_OP_RL_DATA;
    return 0;
}

static int setShowRL_FSize(char *token)
{
    setShowOpt = PSP_OP_RL_FSIZE;
    return 0;
}

static int setShowRL_Locks(char *token)
{
    setShowOpt = PSP_OP_RL_LOCKS;
    return 0;
}

static int setShowRL_MemLock(char *token)
{
    setShowOpt = PSP_OP_RL_MEMLOCK;
    return 0;
}

static int setShowRL_MsgQueue(char *token)
{
    setShowOpt = PSP_OP_RL_MSGQUEUE;
    return 0;
}

static int setShowRL_NoFile(char *token)
{
    setShowOpt = PSP_OP_RL_NOFILE;
    return 0;
}

static int setShowRL_NProc(char *token)
{
    setShowOpt = PSP_OP_RL_NPROC;
    return 0;
}

static int setShowRL_RSS(char *token)
{
    setShowOpt = PSP_OP_RL_RSS;
    return 0;
}

static int setShowRL_SigPending(char *token)
{
    setShowOpt = PSP_OP_RL_SIGPENDING;
    return 0;
}

static int setShowRL_Stack(char *token)
{
    setShowOpt = PSP_OP_RL_STACK;
    return 0;
}

static int setShowError(char *token)
{
    return -1;
}

static keylist_t setShowList[] = {
    {"maxproc", setShowMaxProc},
    {"user", setShowUser},
    {"group", setShowGroup},
    {"adminuser", setShowAdminUser},
    {"admingroup", setShowAdminGroup},
    {"psiddebug", setShowPSIDDebug},
    {"selecttime", setShowSelectTime},
    {"statustimeout", setShowStatusTimeout},
    {"statusbroadcasts", setShowStatBCast},
    {"deadlimit", setShowDeadLimit},
    {"rdpdebug", setShowRDPDebug},
    {"rdptimeout", setShowRDPTmOut},
    {"rdppktloss", setShowRDPPktLoss},
    {"rdpmaxretrans", setShowRDPMaxRetrans},
    {"rdpmaxackpend", setShowRDPMaxACKPend},
    {"rdpresendtimeout", setShowRDPRsndTmOut},
    {"rdpclosedtimeout", setShowRDPClsdTmOut},
    {"rdpretrans", setShowRDPRetrans},
    {"master", setShowMaster},
    {"mcastdebug", setShowMCastDebug},
    {"freeonsuspend", setShowFOS},
    {"fos", setShowFOS},
    {"handleoldbins", setShowHOB},
    {"hob", setShowHOB},
    {"nodessort", setShowNodesSort},
    {"overbook", setShowOverbook},
    {"exclusive", setShowExclusive},
    {"runjobs", setShowRunJobs},
    {"starter", setShowStarter},
    {"pinprocs", setShowPinProcs},
    {"bindmem", setShowBindMem},
    {"cpumap", setShowCPUMap},
    {"accounters", setShowAccounter},
    {"accountpoll", setShowAcctPoll},
    {"supplementarygroups", setShowSupplGrps},
    {"rl_as", setShowRL_AS},
    {"rl_addressspace", setShowRL_AS},
    {"rl_core", setShowRL_Core},
    {"rl_cpu", setShowRL_CPU},
    {"rl_data", setShowRL_Data},
    {"rl_fsize", setShowRL_FSize},
    {"rl_locks", setShowRL_Locks},
    {"rl_memlock", setShowRL_MemLock},
    {"rl_msgqueue", setShowRL_MsgQueue},
    {"rl_nofile", setShowRL_NoFile},
    {"rl_nproc", setShowRL_NProc},
    {"rl_rss", setShowRL_RSS},
    {"rl_sigpending", setShowRL_SigPending},
    {"rl_stack", setShowRL_Stack},
    {NULL, setShowError}
};
static parser_t setShowParser = {" \t\n", setShowList};



static char* origToken;
static long sortMode;

static int sortLoad1or15(char *token)
{
    sortMode = PART_SORT_LOAD_1;
    if ((strcasecmp(origToken, "load15")==0)
	|| (strcasecmp(origToken, "load_15")==0)) {
	sortMode = PART_SORT_LOAD_15;
    }
    return 0;
}

static int sortLoad5(char *token)
{
    sortMode = PART_SORT_LOAD_5;
    return 0;
}

static int sortProcOrProcLoad(char *token)
{
    const char discr[]="proc+";
    sortMode = PART_SORT_PROC;
    if (strncasecmp(origToken, discr, strlen(discr))==0) {
	sortMode = PART_SORT_PROCLOAD;
    }
    return 0;
}

static int sortNone(char *token)
{
    sortMode = PART_SORT_NONE;
    return 0;
}

static keylist_t sort_list[] = {
    {"load15", sortLoad1or15},
    {"load_15", sortLoad1or15},
    {"load5", sortLoad5},
    {"load_5", sortLoad5},
    {"proc+load", sortProcOrProcLoad},
    {"none", sortNone},
    {NULL, parser_error}
};

static parser_t sort_parser = {" \t\n", sort_list};


static PSIADM_valList_t *cpusFromString(char *valStr)
{
    PSIADM_valList_t *valList = malloc(sizeof(*valList));
    char *start=valStr, *end;
    size_t maxCPUs = 32;

    if (!valList) goto error;
    valList->num = 0;
    valList->value = malloc(maxCPUs*sizeof(*valList->value));
    if (!valList->value) goto error;

    if (!valStr) goto error;

    while (*start) {
	long val;

	val = strtol(start, &end, 10);
	if (start==end || (*end && !isspace(*end))) goto error;

	valList->value[valList->num++] = val;
	if (valList->num == maxCPUs) {
	    maxCPUs *= 2;
	    valList->value = realloc(valList->value,
				     maxCPUs*sizeof(*valList->value));
	    if (!valList->value) goto error;
	}

	start=end;
	while (isspace(*start)) start++;
    }
    return valList;

 error:
    if (valList) {
	if (valList->value) free(valList->value);
	free(valList);
    }
    return NULL;
}

static int setCommand(char *token)
{
    char *what = parser_getString();
    char *value = parser_getQuotedString();
    char *nl_descr = parser_getString();
    char *nl = defaultNL;
    long val;
    PSIADM_valList_t *valList = NULL;

    if (!what || !value) goto error;

    setShowOpt = PSP_OP_UNKNOWN;
    if (parser_parseToken(what, &setShowParser)) goto error;

    switch (setShowOpt) {
    case PSP_OP_PROCLIMIT:
	val = procsFromString(value);
	if (val == -2) goto error;
	break;
    case PSP_OP_UID:
    case PSP_OP_ADMUID:
    {
	PSP_Option_t opt = setShowOpt;
	if (*value == '+') {
	    setShowOpt = (opt==PSP_OP_UID) ? PSP_OP_ADD_UID:PSP_OP_ADD_ADMUID;
	    value++;
	} else if (*value == '-') {
	    setShowOpt = (opt==PSP_OP_UID) ? PSP_OP_REM_UID:PSP_OP_REM_ADMUID;
	    value++;
	} else {
	    setShowOpt = (opt==PSP_OP_UID) ? PSP_OP_SET_UID:PSP_OP_SET_ADMUID;
	}
	if (!*value) {
	    value = nl_descr;
	    nl_descr = parser_getString();
	}
	val = uidFromString(value);
	if (val == -2) goto error;
	break;
    }
    case PSP_OP_GID:
    case PSP_OP_ADMGID:
    {
	PSP_Option_t opt = setShowOpt;
	if (*value == '+') {
	    setShowOpt = (opt==PSP_OP_GID) ? PSP_OP_ADD_GID:PSP_OP_ADD_ADMGID;
	    value++;
	} else if (*value == '-') {
	    setShowOpt = (opt==PSP_OP_GID) ? PSP_OP_REM_GID:PSP_OP_REM_ADMGID;
	    value++;
	} else {
	    setShowOpt = (opt==PSP_OP_GID) ? PSP_OP_SET_GID:PSP_OP_SET_ADMGID;
	}
	if (!*value) {
	    value = nl_descr;
	    nl_descr = parser_getString();
	}
	val = gidFromString(value);
	if (val == -2) goto error;
	break;
    }
    case PSP_OP_PSIDDEBUG:
    case PSP_OP_PSIDSELECTTIME:
    case PSP_OP_STATUS_TMOUT:
    case PSP_OP_STATUS_DEADLMT:
    case PSP_OP_STATUS_BCASTS:
    case PSP_OP_RDPDEBUG:
    case PSP_OP_RDPTMOUT:
    case PSP_OP_RDPPKTLOSS:
    case PSP_OP_RDPMAXRETRANS:
    case PSP_OP_RDPMAXACKPEND:
    case PSP_OP_RDPRSNDTMOUT:
    case PSP_OP_RDPCLSDTMOUT:
    case PSP_OP_RDPRETRANS:
    case PSP_OP_MCASTDEBUG:
    case PSP_OP_ACCTPOLL:
    case PSP_OP_MASTER:
	if (parser_getNumber(value, &val)) {
	    printf("Illegal value '%s'\n", value);
	    goto error;
	}
	break;
    case PSP_OP_OVERBOOK:
	if (strcasecmp(value, "auto") == 0) {
	    val = OVERBOOK_AUTO;
	} else {
	    int tmp, ret = parser_getBool(value, &tmp, NULL);
	    if (ret==-1) {
		printf("Illegal value '%s' is not 'auto' or boolean\n", value);
		goto error;
	    }
	    val = tmp;
	}
	break;
    case PSP_OP_FREEONSUSP:
    case PSP_OP_HANDLEOLD:
    case PSP_OP_EXCLUSIVE:
    case PSP_OP_RUNJOBS:
    case PSP_OP_STARTER:
    case PSP_OP_PINPROCS:
    case PSP_OP_BINDMEM:
    case PSP_OP_SUPPL_GRPS:
    {
	int tmp, ret = parser_getBool(value, &tmp, NULL);
	if (ret==-1) {
	    printf("Illegal value '%s' is not boolean\n", value);
	    goto error;
	}
	val = tmp;
	break;
    }
    case PSP_OP_NODESSORT:
	origToken = value;
	if (parser_parseString(origToken, &sort_parser)) {
	    printf("Illegal value '%s'\n", value);
	    goto error;
	}
	val = sortMode;
	break;
    case PSP_OP_CPUMAP:
	valList = cpusFromString(value);
	if (!valList) {
	    printf("Illegal value '%s' for CPU-map\n", value);
	    goto error;
	}
	break;
    default:
	goto error;
    }

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    switch (setShowOpt) {
    case PSP_OP_PROCLIMIT:
    case PSP_OP_ADD_UID:
    case PSP_OP_REM_UID:
    case PSP_OP_SET_UID:
    case PSP_OP_ADD_ADMUID:
    case PSP_OP_REM_ADMUID:
    case PSP_OP_SET_ADMUID:
    case PSP_OP_ADD_GID:
    case PSP_OP_REM_GID:
    case PSP_OP_SET_GID:
    case PSP_OP_ADD_ADMGID:
    case PSP_OP_REM_ADMGID:
    case PSP_OP_SET_ADMGID:
    case PSP_OP_PSIDDEBUG:
    case PSP_OP_PSIDSELECTTIME:
    case PSP_OP_STATUS_TMOUT:
    case PSP_OP_STATUS_DEADLMT:
    case PSP_OP_STATUS_BCASTS:
    case PSP_OP_RDPDEBUG:
    case PSP_OP_RDPTMOUT:
    case PSP_OP_RDPPKTLOSS:
    case PSP_OP_RDPMAXRETRANS:
    case PSP_OP_RDPMAXACKPEND:
    case PSP_OP_RDPRSNDTMOUT:
    case PSP_OP_RDPCLSDTMOUT:
    case PSP_OP_RDPRETRANS:
    case PSP_OP_MCASTDEBUG:
    case PSP_OP_OVERBOOK:
    case PSP_OP_FREEONSUSP:
    case PSP_OP_HANDLEOLD:
    case PSP_OP_EXCLUSIVE:
    case PSP_OP_RUNJOBS:
    case PSP_OP_STARTER:
    case PSP_OP_PINPROCS:
    case PSP_OP_BINDMEM:
    case PSP_OP_NODESSORT:
    case PSP_OP_ACCTPOLL:
    case PSP_OP_SUPPL_GRPS:
    case PSP_OP_MASTER:
	PSIADM_SetParam(setShowOpt, val, nl);
	break;
    case PSP_OP_CPUMAP:
	PSIADM_SetParamList(setShowOpt, valList, nl);
	break;
    default:
	goto error;
    }

    if (valList) {
	if (valList->value) free(valList->value);
	free(valList);
    }

    return 0;

 error:
    printError(&setInfo);
    return -1;
}

static int showCommand(char *token)
{
    char *what = parser_getString();
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString() || !what) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    setShowOpt = PSP_OP_UNKNOWN;
    if (parser_parseToken(what, &setShowParser)) goto error;

    switch (setShowOpt) {
    case PSP_OP_PROCLIMIT:
    case PSP_OP_PSIDDEBUG:
    case PSP_OP_PSIDSELECTTIME:
    case PSP_OP_STATUS_TMOUT:
    case PSP_OP_STATUS_DEADLMT:
    case PSP_OP_STATUS_BCASTS:
    case PSP_OP_RDPDEBUG:
    case PSP_OP_RDPTMOUT:
    case PSP_OP_RDPPKTLOSS:
    case PSP_OP_RDPMAXRETRANS:
    case PSP_OP_RDPMAXACKPEND:
    case PSP_OP_RDPRSNDTMOUT:
    case PSP_OP_RDPCLSDTMOUT:
    case PSP_OP_RDPRETRANS:
    case PSP_OP_MCASTDEBUG:
    case PSP_OP_MASTER:
    case PSP_OP_FREEONSUSP:
    case PSP_OP_HANDLEOLD:
    case PSP_OP_NODESSORT:
    case PSP_OP_OVERBOOK:
    case PSP_OP_EXCLUSIVE:
    case PSP_OP_STARTER:
    case PSP_OP_RUNJOBS:
    case PSP_OP_PINPROCS:
    case PSP_OP_BINDMEM:
    case PSP_OP_ACCTPOLL:
    case PSP_OP_SUPPL_GRPS:
	PSIADM_ShowParam(setShowOpt, nl);
	break;
    case PSP_OP_ACCT:
    case PSP_OP_UID:
    case PSP_OP_GID:
    case PSP_OP_ADMUID:
    case PSP_OP_ADMGID:
    case PSP_OP_RL_AS:
    case PSP_OP_RL_CORE:
    case PSP_OP_RL_CPU:
    case PSP_OP_RL_DATA:
    case PSP_OP_RL_FSIZE:
    case PSP_OP_RL_LOCKS:
    case PSP_OP_RL_MEMLOCK:
    case PSP_OP_RL_MSGQUEUE:
    case PSP_OP_RL_NOFILE:
    case PSP_OP_RL_NPROC:
    case PSP_OP_RL_RSS:
    case PSP_OP_RL_SIGPENDING:
    case PSP_OP_RL_STACK:
    case PSP_OP_CPUMAP:
	PSIADM_ShowParamList(setShowOpt, nl);
	break;
    default:
	goto error;
    }

    return 0;

 error:
    printError(&showInfo);
    return -1;
}

static int killCommand(char *token)
{
    int signal = -1;
    PStask_ID_t tid;
    long tmp;

    token = parser_getString();
    if (!token) goto error;
    if (parser_getNumber(token, &tmp)) goto error;

    token = parser_getString();
    if (token) {
	signal = -tmp;
	if (signal < 0) goto error;
	if (parser_getNumber(token, &tmp)) goto error;
    }
    tid = tmp;
    if (tid < 0 || parser_getString()) goto error;

    PSIADM_KillProc(tid, signal);
    return 0;

 error:
    printError(&killInfo);
    return -1;
}

static int testCommand(char *token)
{
    char *option = parser_getString();
    int verbose = 1;

    if (option) {
	if (!strcasecmp(option, "verbose")) {
	    verbose = 2;
	} else if (!strcasecmp(option, "quiet")) {
	    verbose = 0;
	} else if (!strcasecmp(option, "normal")) {
	} else goto error;
    }

    PSIADM_TestNetwork(verbose);
    return 0;

 error:
    printError(&testInfo);
    return -1;
}

/************************** help commands *******************************/

static int pluginAddCommand(char *token)
{
    char *plugin = parser_getString();
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString() || !plugin) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) return -1;
    }

    PSIADM_Plugin(nl, plugin, PSP_PLUGIN_LOAD);

    return 0;

 error:
    printError(&pluginInfo);
    return -1;
}

static int pluginRmCommand(char *token)
{
    char *plugin = parser_getString();
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString() || !plugin) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) return -1;
    }

    PSIADM_Plugin(nl, plugin, PSP_PLUGIN_REMOVE);

    return 0;

 error:
    printError(&pluginInfo);
    return -1;
}

int pluginError(char *token)
{
    printError(&pluginInfo);
    return -1;
}

static keylist_t pluginList[] = {
    {"list", listPluginCommand},
    {"load", pluginAddCommand},
    {"add", pluginAddCommand},
    {"unload", pluginRmCommand},
    {"delete", pluginRmCommand},
    {"remove", pluginRmCommand},
    {"rm", pluginRmCommand},
    {"unload", pluginRmCommand},
    {NULL, pluginError}
};
static parser_t pluginParser = {" \t\n", pluginList};

static int pluginCommand(char *token)
{
    char *what = parser_getString();

    if (!what) listPluginCommand(what);

    return parser_parseString(what, &pluginParser);
}

/************************** help commands *******************************/

static int helpAdd(char *token)
{
    printInfo(&addInfo);
    return 0;
}

static int helpShutdown(char *token)
{
    printInfo(&shutdownInfo);
    return 0;
}

static int helpHWStart(char *token)
{
    printInfo(&hwstartInfo);
    return 0;
}

static int helpHWStop(char *token)
{
    printInfo(&hwstopInfo);
    return 0;
}

static int helpListCmd(char *token)
{
    printInfo(&listInfo);
    return 0;
}

static int helpRange(char *token)
{
    printInfo(&rangeInfo);
    return 0;
}

static int helpReset(char *token)
{
    printInfo(&resetInfo);
    return 0;
}

static int helpRestart(char *token)
{
    printInfo(&restartInfo);
    return 0;
}

static int helpVersion(char *token)
{
    printInfo(&versionInfo);
    return 0;
}

static int helpExit(char *token)
{
    printInfo(&exitInfo);
    return 0;
}

static int helpSet(char *token)
{
    printInfo(&setInfo);
    return 0;
}

static int helpShow(char *token)
{
    printInfo(&showInfo);
    return 0;
}

static int helpKill(char *token)
{
    printInfo(&killInfo);
    return 0;
}

static int helpTest(char *token)
{
    printInfo(&testInfo);
    return 0;
}

static int helpResolve(char *token)
{
    printInfo(&resolveInfo);
    return 0;
}

static int helpSleep(char *token)
{
    printInfo(&sleepInfo);
    return 0;
}

static int helpNodes(char *token)
{
    printInfo(&nodeInfo);
    return 0;
}

static int helpPlugins(char *token)
{
    printInfo(&pluginInfo);
    return 0;
}

static int helpHelp(char *token)
{
    printInfo(&helpInfo);
    printInfo(&privilegedInfo);
    return 0;
}

static int helpNotFound(char *token)
{
    return -1;
}

static keylist_t helpList[] = {
    {"add", helpAdd},
    {"shutdown", helpShutdown},
    {"hwstart", helpHWStart},
    {"hwstop", helpHWStop},
    {"list", helpListCmd},
    {"status", helpListCmd},
    {"range", helpRange},
    {"reset", helpReset},
    {"restart", helpRestart},
    {"version", helpVersion},
    {"exit", helpExit},
    {"quit", helpExit},
    {"set", helpSet},
    {"show", helpShow},
    {"kill", helpKill},
    {"test", helpTest},
    {"resolve", helpResolve},
    {"plugins", helpPlugins},
    {"sleep", helpSleep},
    {"nodes", helpNodes},
    {"help", helpHelp},
    {NULL, helpNotFound}
};
static parser_t helpParser = {" \t\n", helpList};

static int helpCommand(char *token)
{
    char *topic = parser_getString();

    if (topic) {
	if (parser_parseToken(topic, &helpParser)) {
	    printError(&helpInfo);
	    return -1;
	}
    } else {
	printInfo(&helpInfo);
	if (!getuid()) printInfo(&privilegedInfo);
	printf("For more information type 'help <command>'\n\n");
    }

    while (parser_getString());

    return 0;
}

static int versionCommand(char *token)
{
    extern char psiadmversion[];
    char tmp[100];
    int err;

    if (parser_getString()) goto error;

    printf("PSIADMIN: ParaStation administration tool\n");
    printf("Copyright (C) 1996-2004 ParTec AG Karlsruhe\n");
    printf("Copyright (C) 2005-2009 ParTec Cluster Competence Center GmbH, Munich\n");
    printf("\n");
    printf("PSIADMIN:   %s\b/ %s\b/ %s\b \b\b\n", psiadmversion+11,
	   commandsversion+11, parserversion+11);
    printf("PSProtocol: %d\n", PSProtocolVersion);
    printf("RPM:        %s-%s\n", VERSION_psmgmt, RELEASE_psmgmt);

    err = PSI_infoString(-1, PSP_INFO_DAEMONVER, NULL, tmp, sizeof(tmp), 0);
    if (err) strcpy(tmp, "$Revision: unknown$");
    printf("PSID:       %s\b \n", tmp+11);

    err = PSI_infoString(-1, PSP_INFO_RPMREV, NULL, tmp, sizeof(tmp), 0);
    if (err) strcpy(tmp, "unknown");
    printf("RPM:        %s\n", tmp);
    return 0;

 error:
    printError(&versionInfo);
    return -1;
}

static int sleepCommand(char *token)
{
    long tmp;

    token = parser_getString();
    if (!token) goto error;
    if (parser_getNumber(token, &tmp)) goto error;

    if (tmp < 0 || parser_getString()) goto error;

    sleep(tmp);
    return 0;

 error:
    printError(&sleepInfo);
    return -1;
}

static int resolveCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_Resolve(nl);
    return 0;

 error:
    printError(&resolveInfo);
    return -1;
}

/** Magic value returned by the parser function to show 'quit' was reached. */
#define quitMagic 17

static int quitCommand(char *token)
{
    if (parser_getString()) goto error;
    return quitMagic;

 error:
    printError(&exitInfo);
    return -1;
}

static int error(char *token)
{
    printf("Syntax error\n");

    return -1;
}

static keylist_t commandList[] = {
    {"add", addCommand},
    {"shutdown", shutdownCommand},
    {"hwstart", hwstartCommand},
    {"hwstop", hwstopCommand},
    {"range", rangeCommand},
    {"restart", restartCommand},
    {"reset", resetCommand},
    {"list", listCommand},
    {"status", listCommand},
    {"show", showCommand},
    {"set", setCommand},
    {"kill", killCommand},
    {"test", testCommand},
    {"?", helpCommand},
    {"help", helpCommand},
    {"version", versionCommand},
    {"sleep", sleepCommand},
    {"resolve", resolveCommand},
    {"plugins", pluginCommand},
    {"exit", quitCommand},
    {"quit", quitCommand},
    {NULL, error}
};
static parser_t commandParser = {" \t\n", commandList};

static int parseCommand(char *command)
{
    char *token, *line = parser_getLine();
    int ret;

    /* Register to new parser */
    token = parser_registerString(command, &commandParser);
    ret = parser_parseString(token, &commandParser);
    if (ret) return ret;

    /* Now handle the rest of the line */
    ret = parseLine(line);

    return ret;
}

static keylist_t lineList[] = {
    {NULL, parseCommand}
};
static parser_t lineParser = {";\n", lineList};

int parseLine(char *line)
{
    char *token;

    static int firstCall = 1;

    if (firstCall) {
	parser_init(0, NULL);
	parser_setDebugMask(0);
	setupDefaultNL();
	firstCall = 0;
    }

    /* Put line into strtok() */
    token = parser_registerString(line, &lineParser);

    /* Do the parsing */
    return (parser_parseString(token, &lineParser) == quitMagic);
}

#include <readline/readline.h>

static keylist_t *genList = NULL;

static char *generator(const char *text, int state)
{
    static int index, len;
    char *name, *ret=NULL;

    /* If this is a new word to complete, initialize now.  This
       includes saving the length of TEXT for efficiency, and
       initializing the index variable to 0. */
    if (!state) {
	index = 0;
	len = strlen (text);
    }

    /* Return the next name which partially matches from the
       command list. */
    if (genList) {
	while ((name = genList[index].key) && !ret) {
	    if (!strncmp(name, text, len))
		ret = strdup(name);
	    index++;
	}
    }

    return ret;
}

char **completeLine(const char *text, int start, int end)
{
    int tokStart = 0, tokEnd = start-1;
    char **matches = NULL;

    genList = NULL;
    rl_attempted_completion_over = 1;

    /* Try to find a token in front of the text to complete */
    while (isspace(rl_line_buffer[tokStart])) tokStart++;

    if (tokStart == start) {
	genList = commandList;
    } else {
	char *token = &rl_line_buffer[tokStart];

	while (isspace(rl_line_buffer[tokEnd])) tokEnd--;

	if (!strncmp(token, "help", tokEnd-tokStart+1)
	    || !strncmp(token, "?", tokEnd-tokStart+1)) {
	    genList = helpList;
	} else if (!strncmp(token, "set", tokEnd-tokStart+1)) {
	    genList = setShowList;
	} else if (!strncmp(token, "show", tokEnd-tokStart+1)) {
	    genList = setShowList;
	} else if (!strncmp(token, "status", tokEnd-tokStart+1)) {
	    genList = listList;
	} else if (!strncmp(token, "list", tokEnd-tokStart+1)) {
	    genList = listList;
	} else if (!strncmp(token, "plugins", tokEnd-tokStart+1)) {
	    genList = pluginList;
	}
    }
    matches = rl_completion_matches(text, generator);

    return (matches);
}
