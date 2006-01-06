/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2006 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char lexid[] __attribute__(( unused )) = "$Id$";
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

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) return -1;
    }

    if (parser_getString()) goto error;

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

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) return -1;
    }

    if (parser_getString()) goto error;

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

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) goto error;
    }
    if (parser_getString()) goto error; /* trailing garbage */

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
    int cnt = 10;

    if (nl_descr && !strcasecmp(nl_descr, "cnt")) {
	char *tok = parser_getString();
	if (!tok) goto error;
	cnt = parser_getNumber(tok);
	if (cnt<0) goto error;
	nl_descr = parser_getString();
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) goto error;
    }
    if (parser_getString()) goto error; /* trailing garbage */

    if (!strcasecmp(token, "proc")) {
	PSIADM_ProcStat(cnt, 0, nl);
    } else if (!strcasecmp(token, "aproc")) {
	PSIADM_ProcStat(cnt, 1, nl);
    } else goto error;
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listNodeCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) goto error;
    }
    if (parser_getString()) goto error; /* trailing garbage */

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

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) goto error;
    }
    if (parser_getString()) goto error; /* trailing garbage */

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

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) goto error;
    }
    if (parser_getString()) goto error; /* trailing garbage */

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

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) goto error;
    }
    if (parser_getString()) goto error; /* trailing garbage */

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

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) goto error;
    }
    if (parser_getString()) goto error; /* trailing garbage */

    PSIADM_LoadStat(nl);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listHWCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) goto error;
    }
    if (parser_getString()) goto error; /* trailing garbage */

    PSIADM_HWStat(nl);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listVersionCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) goto error;
    }
    if (parser_getString()) goto error; /* trailing garbage */

    PSIADM_VersionStat(nl);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listSpecialCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = defaultNL;

    if (!nl_descr) {
	/* Maybe this is like 'list a-b' */
	if (token) nl = getNodeList(token);
	if (!nl) goto error;
    } else goto error;

    PSIADM_NodeStat(nl);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static keylist_t listList[] = {
    {"aproc", listProcCommand},
    {"count", listCountCommand},
    {"hardware", listHWCommand},
    {"hw", listHWCommand},
    {"load", listLoadCommand},
    {"mcast", listMCastCommand},
    {"node", listNodeCommand},
    {"proc", listProcCommand},
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

static long procsFromString(char *procs)
{
    long tmp = parser_getNumber(procs);

    if (strcasecmp(procs, "any") == 0) return -1;
    if (tmp > -1) return tmp;

    printf("Unknown value '%s'\n", procs);
    return -2;
}

static uid_t uidFromString(char *user)
{
    long tmp = parser_getNumber(user);
    struct passwd *passwd = getpwnam(user);

    if (strcasecmp(user, "any") == 0) return -1;
    if (tmp > -1) return tmp;
    if (passwd) return passwd->pw_uid;

    printf("Unknown user '%s'\n", user);
    return -2;
}

static gid_t gidFromString(char *group)
{
    long tmp = parser_getNumber(group);
    struct group *grp = getgrnam(group);

    if (strcasecmp(group, "any") == 0) return -1;
    if (tmp > -1) return tmp;
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
    setShowOpt = PSP_OP_UIDLIMIT;
    return 0;
}

static int setShowGroup(char *token)
{
    setShowOpt = PSP_OP_GIDLIMIT;
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

static int setShowRDPDebug(char *token)
{
    setShowOpt = PSP_OP_RDPDEBUG;
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

static int setShowSPS(char *token)
{
    setShowOpt = PSP_OP_PSM_SPS;
    return 0;
}

static int setShowRTO(char *token)
{
    setShowOpt = PSP_OP_PSM_RTO;
    return 0;
}

static int setShowHNPend(char *token)
{
    setShowOpt = PSP_OP_PSM_HNPEND;
    return 0;
}

static int setShowAckPend(char *token)
{
    setShowOpt = PSP_OP_PSM_ACKPEND;
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

static int setShowError(char *token)
{
    return -1;
}

static keylist_t setShowList[] = {
    {"maxproc", setShowMaxProc},
    {"user", setShowUser},
    {"group", setShowGroup},
    {"psiddebug", setShowPSIDDebug},
    {"selecttime", setShowSelectTime},
    {"rdpdebug", setShowRDPDebug},
    {"rdppktloss", setShowRDPPktLoss},
    {"rdpmaxretrans", setShowRDPMaxRetrans},
    {"master", setShowMaster},
    {"mcastdebug", setShowMCastDebug},
    {"smallpacketsize", setShowSPS},
    {"sps", setShowSPS},
    {"resendtimeout", setShowRTO},
    {"rto", setShowRTO},
    {"hnpend", setShowHNPend},
    {"ackpend", setShowAckPend},
    {"freeonsuspend", setShowFOS},
    {"fos", setShowFOS},
    {"handleoldbins", setShowHOB},
    {"hob", setShowHOB},
    {"nodessort", setShowNodesSort},
    {"overbook", setShowOverbook},
    {"runjobs", setShowRunJobs},
    {"starter", setShowStarter},
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


static int setCommand(char *token)
{
    char *what = parser_getString();
    char *value = parser_getString();
    char *nl_descr = parser_getString();
    char *nl = defaultNL;
    long val;

    if (parser_getString() || !what || !value) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    setShowOpt = PSP_OP_UNKNOWN;
    if (parser_parseToken(what, &setShowParser)) goto error;

    switch (setShowOpt) {
    case PSP_OP_PROCLIMIT:
	val = procsFromString(value);
	if (val == -2) goto error;
	break;
    case PSP_OP_UIDLIMIT:
	val = uidFromString(value);
	if (val == -2) goto error;
	break;
    case PSP_OP_GIDLIMIT:
	val = gidFromString(value);
	if (val == -2) goto error;
	break;
    case PSP_OP_PSIDDEBUG:
    case PSP_OP_PSIDSELECTTIME:
    case PSP_OP_RDPDEBUG:
    case PSP_OP_RDPPKTLOSS:
    case PSP_OP_RDPMAXRETRANS:
    case PSP_OP_MCASTDEBUG:
    case PSP_OP_PSM_SPS:
    case PSP_OP_PSM_RTO:
    case PSP_OP_PSM_HNPEND:
    case PSP_OP_PSM_ACKPEND:
	val = parser_getNumber(value);
	if (val==-1) {
	    printf("Illegal value '%s'\n", value);
	    goto error;
	}
	break;
    case PSP_OP_FREEONSUSP:
    case PSP_OP_HANDLEOLD:
    case PSP_OP_OVERBOOK:
    case PSP_OP_RUNJOBS:
    case PSP_OP_STARTER:
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
    default:
	goto error;
    }
    PSIADM_SetParam(setShowOpt, val, nl);
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
    case PSP_OP_UIDLIMIT:
    case PSP_OP_GIDLIMIT:
    case PSP_OP_PSIDDEBUG:
    case PSP_OP_PSIDSELECTTIME:
    case PSP_OP_RDPDEBUG:
    case PSP_OP_RDPPKTLOSS:
    case PSP_OP_RDPMAXRETRANS:
    case PSP_OP_MCASTDEBUG:
    case PSP_OP_MASTER:
    case PSP_OP_PSM_SPS:
    case PSP_OP_PSM_RTO:
    case PSP_OP_PSM_HNPEND:
    case PSP_OP_PSM_ACKPEND:
    case PSP_OP_FREEONSUSP:
    case PSP_OP_HANDLEOLD:
    case PSP_OP_NODESSORT:
    case PSP_OP_OVERBOOK:
    case PSP_OP_STARTER:
    case PSP_OP_RUNJOBS:
	break;
    default:
	goto error;
    }

    PSIADM_ShowParam(setShowOpt, nl);
    return 0;

 error:
    printError(&showInfo);
    return -1;
}

static int killCommand(char *token)
{
    int signal = -1;
    PStask_ID_t tid;

    token = parser_getString();
    if (!token) goto error;
    tid = parser_getNumber(token);

    token = parser_getString();
    if (token) {
	signal = -tid;
	if (signal < 0) goto error;
	tid = parser_getNumber(token);
    }
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

static int helpNodes(char *token)
{
    printInfo(&nodeInfo);
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
    {"nodes", helpNodes},
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
    printf("Copyright (C) 2005-2006 Cluster Competence Center GmbH, Munich\n");
    printf("\n");
    printf("PSIADMIN:   %s\b/ %s\b/ %s\b \b\b\n", psiadmversion+11,
	   commandsversion+11, parserversion+11);
    printf("PSProtocol: %d\n", PSprotocolVersion);
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
    {"exit", quitCommand},
    {"quit", quitCommand},
    {NULL, error}
};
static parser_t commandParser = {" \t\n", commandList};

static int parseCommand(char *command)
{
    char *token, *line = parser_getLine();
    int ret;

    // printf("Command is '%s'\n", command);

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
	}
    }
    matches = rl_completion_matches(text, generator);

    return (matches);
}
