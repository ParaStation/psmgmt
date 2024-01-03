/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "adminparser.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <unistd.h>

#include "pscommon.h"
#include "pspartition.h"
#include "psi.h"
#include "psiinfo.h"
#include "parser.h"
#include "psparamspace.h"

#include "commands.h"

#include "helpmsgs.c"

static bool *defaultNL = NULL;

static bool setupDefaultNL(void)
{
    defaultNL = malloc(PSC_getNrOfNodes() * sizeof(*defaultNL));
    if (!defaultNL) return false;

    for (int n = 0; n < PSC_getNrOfNodes(); n++) defaultNL[n] = true;
    return true;
}

static void releaseDefaultNL(void)
{
    free(defaultNL);
    defaultNL = NULL;
}

static bool * getNodeList(char *nl_descr)
{
    static bool *nl = NULL;
    char *host, *work = NULL;

    if (!nl) nl = malloc(sizeof(*nl) * PSC_getNrOfNodes());
    if (!nl) {
	PSC_log(-1, "%s: no memory\n", __func__);
	return NULL;
    }

    memset(nl, 0, sizeof(*nl) * PSC_getNrOfNodes());
    if (!strcasecmp(nl_descr, "all")) {
	for (int n = 0; n < PSC_getNrOfNodes(); n++) nl[n] = true;
	return nl;
    } else {
	host = strdup(nl_descr);
	bool *ret = PSC_parseNodelist(host);
	free(host);
	if (ret) return ret;
    }

    host = strtok_r(nl_descr, ",", &work);
    while (host) {
	PSnodes_ID_t node = PSI_resolveNodeID(host);

	if (node < 0) {
	    printf("Illegal nodename '%s'\n", host);
	    return NULL;
	}

	nl[node] = true;
	host = strtok_r(NULL, ",", &work);
    }
    return nl;
}

/*************************** simple commands ****************************/

static int addCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&addInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_AddNode(nl);
    return 0;
}

static int shutdownCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;
    int silent = 0;

    if (nl_descr && !strcasecmp(nl_descr, "silent")) {
	silent = 1;
	nl_descr = parser_getString();
    }

    if (parser_getString()) {
	printError(&shutdownInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_ShutdownNode(silent, nl);
    return 0;
}

static int hwstartCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;
    char *hw = NULL;
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
	int err = PSI_infoInt(-1, PSP_INFO_HWINDEX, hw, &hwIndex, true);
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
    bool *nl = defaultNL;
    char *hw = NULL;
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
	int err = PSI_infoInt(-1, PSP_INFO_HWINDEX, hw, &hwIndex, true);
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
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&restartInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_Reset(1, nl);
    return 0;
}

static char * printRange(void *data)
{
    PSC_printNodelist(defaultNL);

    return strdup("");
}

static int rangeCommand(char *token)
{
    char *nl_descr = parser_getString();

    if (parser_getString()) {
	printError(&rangeInfo);
	return -1;
    }

    if (nl_descr) {
	bool *nl = getNodeList(nl_descr);
	if (!nl) return -1;

	memcpy(defaultNL, nl, PSC_getNrOfNodes());
    } else {
	char *res;
	printf(" ");
	res = printRange(NULL);
	printf("\n");
	free(res);
    }

    return 0;
}

static int resetCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;
    int hw = 0;

    if (nl_descr && !strcasecmp(nl_descr, "hw")) {
	nl_descr = parser_getString();
	hw = 1;
    }

    if (parser_getString()) {
	printError(&resetInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_Reset(hw, nl);
    return 0;
}

/**************************** list commands *****************************/
static int listCountCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;
    int hwIndex = -1;

    if (nl_descr && !strcasecmp(nl_descr, "hw")) {
	char *hw = parser_getString();
	if (hw) {
	    int err = PSI_infoInt(-1, PSP_INFO_HWINDEX, hw, &hwIndex, true);
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
    bool *nl = defaultNL;
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
    bool *nl = defaultNL;

    if (parser_getString()) {
	if (token && !strcasecmp(token, "plugin")) {
	    printError(&pluginInfo);
	} else {
	    printError(&listInfo);
	}
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_PluginStat(nl);
    return 0;
}

static int listEnvCommand(char *token)
{
    char *key = NULL, *nl_descr = parser_getString();
    bool *nl = defaultNL;


    if (nl_descr && !strcasecmp(nl_descr, "key")) {
	key = parser_getString();
	if (!key) goto error;
	nl_descr = parser_getString();
    }

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_EnvStat(key, nl);
    return 0;

 error:
    if (token && !strcasecmp(token, "environment")) {
	printError(&envInfo);
    } else {
	printError(&listInfo);
    }
    return -1;
}

static int listNodeCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&listInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_NodeStat(nl);
    return 0;
}

static int listSomeCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&listInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_SomeStat(nl, token[0]);
    return 0;
}

static int listSummaryCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;
    long max = 20;

    while (nl_descr) {
	int tokLen = strlen(nl_descr);
	if (!strncasecmp(nl_descr, "max", tokLen)) {
	    char *maxStr = parser_getString();
	    if (!maxStr || parser_getNumber(maxStr, &max)) goto error;
	    nl_descr = parser_getString();
	    continue;
	} else break;
    }

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_SummaryStat(nl, max);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listStarttimeCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&listInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_StarttimeStat(nl);
    return 0;
}

static int listScriptCommand(char *token)
{
    PSP_Info_t info;
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString()) goto error;

    if (!strcasecmp(token, "startupscript")) {
	info = PSP_INFO_STARTUPSCRIPT;
    } else if (!strcasecmp(token, "nodeupscript")) {
	info = PSP_INFO_NODEUPSCRIPT;
    } else if (!strcasecmp(token, "nodedownscript")) {
	info = PSP_INFO_NODEDOWNSCRIPT;
    } else {
	printf("Unknown info '%s' to list\n", token);
	goto error;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_ScriptStat(info, nl);
    return 0;

 error:
    printError(&listInfo);
    return -1;
}

static int listRDPCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&listInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_RDPStat(nl);
    return 0;
}

static int listRDPConnCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&listInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_RDPConnStat(nl);
    return 0;
}

static int listMCastCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&listInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_MCastStat(nl);
    return 0;
}

static int listLoadCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&listInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_LoadStat(nl);
    return 0;
}

static int listMemoryCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&listInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_MemStat(nl);
    return 0;
}

static int listHWCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&listInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_HWStat(nl);
    return 0;
}

static int listInstdirCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&listInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_InstdirStat(nl);
    return 0;
}

static int listJobsCommand(char *token)
{
    char *tid_descr = parser_getString();
    PStask_ID_t task = 0;
    PSpart_list_t opt = 0;

    while (tid_descr) {
	if (!strncasecmp(tid_descr, "state", 2)) {
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
	} else if (!strncasecmp(tid_descr, "slots", 2)) {
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
    if (task && (!PSC_validNode(PSC_getID(task))
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
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&listInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_VersionStat(nl);
    return 0;
}

static int listSpecialCommand(char *token)
{
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&listInfo);
	return -1;
    }

    /* Maybe this is like 'list a-b' */
    if (token) {
	nl = getNodeList(token);
	if (!nl) return -1;
    }

    PSIADM_NodeStat(nl);
    return 0;
}

static keylist_t listList[] = {
    {"aproc", listProcCommand, NULL},
    {"allprocesses", listProcCommand, NULL},
    {"count", listCountCommand, NULL},
    {"down", listSomeCommand, NULL},
    {"hardware", listHWCommand, NULL},
    {"hw", listHWCommand, NULL},
    {"installdir", listInstdirCommand, NULL},
    {"jobs", listJobsCommand, NULL},
    {"load", listLoadCommand, NULL},
    {"mcast", listMCastCommand, NULL},
    {"memory", listMemoryCommand, NULL},
    {"node", listNodeCommand, NULL},
    {"processes", listProcCommand, NULL},
    {"p", listProcCommand, NULL},
    {"plugins", listPluginCommand, NULL},
    {"environment", listEnvCommand, NULL},
    {"r", listRDPCommand, NULL},
    {"rdp", listRDPCommand, NULL},
    {"rdpconnection", listRDPConnCommand, NULL},
    {"summary", listSummaryCommand, NULL},
    {"s", listSummaryCommand, NULL},
    {"starttime", listStarttimeCommand, NULL},
    {"startupscript", listScriptCommand, NULL},
    {"nodeupscript", listScriptCommand, NULL},
    {"nodedownscript", listScriptCommand, NULL},
    {"up", listSomeCommand, NULL},
    {"versions", listVersionCommand, NULL},
    {NULL, listSpecialCommand, NULL}
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

    if (!strcasecmp(procStr, "any")) return -1;
    if (!parser_getNumber(procStr, &procs) && procs > -1) return procs;

    printf("Unknown value '%s'\n", procStr);
    return -2;
}

static long sortMode;

static int sortLoad1(char *token)
{
    sortMode = PART_SORT_LOAD_1;
    return 0;
}

static int sortLoad5(char *token)
{
    sortMode = PART_SORT_LOAD_5;
    return 0;
}

static int sortLoad15(char *token)
{
    sortMode = PART_SORT_LOAD_15;
    return 0;
}

static int sortProc(char *token)
{
    sortMode = PART_SORT_PROC;
    return 0;
}

static int sortProcLoad(char *token)
{
    sortMode = PART_SORT_PROCLOAD;
    return 0;
}

static int sortNone(char *token)
{
    sortMode = PART_SORT_NONE;
    return 0;
}

static keylist_t boolList[] = {
    {"yes", NULL, NULL},
    {"no", NULL, NULL},
    {"true", NULL, NULL},
    {"false", NULL, NULL},
    {NULL, NULL, NULL}
};

static keylist_t boolAutoList[] = {
    {"auto", NULL, NULL},
    {"yes", NULL, NULL},
    {"no", NULL, NULL},
    {"true", NULL, NULL},
    {"false", NULL, NULL},
    {NULL, NULL, NULL}
};

static keylist_t numOrUnlimitedList[] = {
    {"<num>", NULL, NULL},
    {"unlimited", NULL, NULL},
    {NULL, NULL, NULL}
};

static keylist_t numOrAnyList[] = {
    {"<num>", NULL, NULL},
    {"any", NULL, NULL},
    {NULL, NULL, NULL}
};

static keylist_t userOrAnyList[] = {
    {"<user>", NULL, NULL},
    {"any", NULL, NULL},
    {NULL, NULL, NULL}
};

static keylist_t sortList[] = {
    {"load1", sortLoad1, NULL},
    {"load_1", sortLoad1, NULL},
    {"load5", sortLoad5, NULL},
    {"load_5", sortLoad5, NULL},
    {"load15", sortLoad15, NULL},
    {"load_15", sortLoad15, NULL},
    {"proc", sortProc, NULL},
    {"proc+load", sortProcLoad, NULL},
    {"none", sortNone, NULL},
    {NULL, parser_error, NULL}
};

static parser_t sortParser = {" \t\n", sortList};

static PSP_Option_t setShowOpt = PSP_OP_UNKNOWN;

static int setShowMaxProc(char *token)
{
    setShowOpt = PSP_OP_PROCLIMIT;
    return 0;
}

static int setShowObsTasks(char *token)
{
    setShowOpt = PSP_OP_OBSOLETE;
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

static int setShowRDPStatistics(char *token)
{
    setShowOpt = PSP_OP_RDPSTATISTICS;
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

static int setShowBindGPUs(char *token)
{
    setShowOpt = PSP_OP_BINDGPUS;
    return 0;
}

static int setShowBindNICs(char *token)
{
    setShowOpt = PSP_OP_BINDNICS;
    return 0;
}

static int setShowCPUMap(char *token)
{
    setShowOpt = PSP_OP_CPUMAP;
    return 0;
}

static int setShowAllowUserMap(char *token)
{
    setShowOpt = PSP_OP_ALLOWUSERMAP;
    return 0;
}

static int setShowAccounter(char *token)
{
    setShowOpt = PSP_OP_ACCT;
    return 0;
}

static int setShowKillDelay(char *token)
{
    setShowOpt = PSP_OP_KILLDELAY;
    return 0;
}

static int setShowSupplGrps(char *token)
{
    setShowOpt = PSP_OP_SUPPL_GRPS;
    return 0;
}

static int setShowMaxStatTry(char *token)
{
    setShowOpt = PSP_OP_MAXSTATTRY;
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

static int setShowPluginAPIver(char *token)
{
    setShowOpt = PSP_OP_PLUGINAPIVERSION;
    return 0;
}

static int setShowPluginUnloadTmout(char *token)
{
    setShowOpt = PSP_OP_PLUGINUNLOADTMOUT;
    return 0;
}

static int setShowError(char *token)
{
    return -1;
}

static keylist_t setShowList[] = {
    {"maxproc", setShowMaxProc, numOrAnyList},
    {"obsoletetasks", setShowObsTasks, NULL},
    {"user", setShowUser, userOrAnyList},
    {"group", setShowGroup, userOrAnyList},
    {"adminuser", setShowAdminUser, userOrAnyList},
    {"admingroup", setShowAdminGroup, userOrAnyList},
    {"psiddebug", setShowPSIDDebug, NULL},
    {"selecttime", setShowSelectTime, NULL},
    {"statustimeout", setShowStatusTimeout, NULL},
    {"statusbroadcasts", setShowStatBCast, NULL},
    {"deadlimit", setShowDeadLimit, NULL},
    {"rdpdebug", setShowRDPDebug, NULL},
    {"rdptimeout", setShowRDPTmOut, NULL},
    {"rdppktloss", setShowRDPPktLoss, NULL},
    {"rdpmaxretrans", setShowRDPMaxRetrans, NULL},
    {"rdpmaxackpend", setShowRDPMaxACKPend, NULL},
    {"rdpresendtimeout", setShowRDPRsndTmOut, NULL},
    {"rdpclosedtimeout", setShowRDPClsdTmOut, NULL},
    {"rdpretrans", setShowRDPRetrans, NULL},
    {"rdpstatistics", setShowRDPStatistics, boolList},
    {"master", setShowMaster, NULL},
    {"mcastdebug", setShowMCastDebug, NULL},
    {"freeonsuspend", setShowFOS, boolList},
    {"fos", setShowFOS, boolList},
    {"nodessort", setShowNodesSort, sortList},
    {"overbook", setShowOverbook, boolAutoList},
    {"exclusive", setShowExclusive, boolList},
    {"runjobs", setShowRunJobs, boolList},
    {"starter", setShowStarter, boolList},
    {"pinprocs", setShowPinProcs, boolList},
    {"bindmem", setShowBindMem, boolList},
    {"bindgpus", setShowBindGPUs, boolList},
    {"bindnics", setShowBindNICs, boolList},
    {"cpumap", setShowCPUMap, NULL},
    {"allowusermap", setShowAllowUserMap, boolList},
    {"accounters", setShowAccounter, NULL},
    {"killdelay", setShowKillDelay, NULL},
    {"supplementarygroups", setShowSupplGrps, boolList},
    {"maxstattry", setShowMaxStatTry, NULL},
    {"rl_as", setShowRL_AS, NULL},
    {"rl_addressspace", setShowRL_AS, NULL},
    {"rl_core", setShowRL_Core, numOrUnlimitedList},
    {"rl_cpu", setShowRL_CPU, numOrUnlimitedList},
    {"rl_data", setShowRL_Data, numOrUnlimitedList},
    {"rl_fsize", setShowRL_FSize, numOrUnlimitedList},
    {"rl_locks", setShowRL_Locks, numOrUnlimitedList},
    {"rl_memlock", setShowRL_MemLock, numOrUnlimitedList},
    {"rl_msgqueue", setShowRL_MsgQueue, numOrUnlimitedList},
    {"rl_nofile", setShowRL_NoFile, numOrUnlimitedList},
    {"rl_nproc", setShowRL_NProc, numOrUnlimitedList},
    {"rl_rss", setShowRL_RSS, numOrUnlimitedList},
    {"rl_sigpending", setShowRL_SigPending, numOrUnlimitedList},
    {"rl_stack", setShowRL_Stack, numOrUnlimitedList},
    {"pluginapiversion", setShowPluginAPIver, NULL},
    {"pluginunloadtmout", setShowPluginUnloadTmout, NULL},
    {NULL, setShowError, NULL}
};
static parser_t setShowParser = {" \t\n", setShowList};


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
    if (valList) free(valList->value);
    free(valList);
    return NULL;
}

static int setCommand(char *token)
{
    char *what = parser_getString();
    char *value = parser_getQuotedString();
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;
    PSP_Option_t setOpt;
    long val;
    PSIADM_valList_t *valList = NULL;

    if (!what || !value) goto printError;

    setShowOpt = PSP_OP_UNKNOWN;
    if (parser_parseToken(what, &setShowParser)) goto printError;
    setOpt = setShowOpt;

    switch (setOpt) {
    case PSP_OP_PROCLIMIT:
	val = procsFromString(value);
	if (val == -2) goto printError;
	break;
    case PSP_OP_UID:
    case PSP_OP_ADMUID:
    {
	PSP_Option_t opt = setOpt;
	if (*value == '+') {
	    setOpt = (opt==PSP_OP_UID) ? PSP_OP_ADD_UID:PSP_OP_ADD_ADMUID;
	    value++;
	} else if (*value == '-') {
	    setOpt = (opt==PSP_OP_UID) ? PSP_OP_REM_UID:PSP_OP_REM_ADMUID;
	    value++;
	} else {
	    setOpt = (opt==PSP_OP_UID) ? PSP_OP_SET_UID:PSP_OP_SET_ADMUID;
	}
	if (!*value) {
	    value = nl_descr;
	    nl_descr = parser_getString();
	}
	val = PSC_uidFromString(value);
	if (val == -2) goto printError;
	break;
    }
    case PSP_OP_GID:
    case PSP_OP_ADMGID:
    {
	PSP_Option_t opt = setOpt;
	if (*value == '+') {
	    setOpt = (opt==PSP_OP_GID) ? PSP_OP_ADD_GID:PSP_OP_ADD_ADMGID;
	    value++;
	} else if (*value == '-') {
	    setOpt = (opt==PSP_OP_GID) ? PSP_OP_REM_GID:PSP_OP_REM_ADMGID;
	    value++;
	} else {
	    setOpt = (opt==PSP_OP_GID) ? PSP_OP_SET_GID:PSP_OP_SET_ADMGID;
	}
	if (!*value) {
	    value = nl_descr;
	    nl_descr = parser_getString();
	}
	val = PSC_gidFromString(value);
	if (val == -2) goto printError;
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
    case PSP_OP_KILLDELAY:
    case PSP_OP_MASTER:
    case PSP_OP_MAXSTATTRY:
    case PSP_OP_PLUGINUNLOADTMOUT:
    case PSP_OP_OBSOLETE:
	if (parser_getNumber(value, &val)) {
	    printf("Illegal value '%s'\n", value);
	    goto printError;
	}
	break;
    case PSP_OP_OVERBOOK:
	if (!strcasecmp(value, "auto")) {
	    val = OVERBOOK_AUTO;
	} else {
	    int tmp, ret = parser_getBool(value, &tmp, NULL);
	    if (ret==-1) {
		printf("Illegal value '%s' is not 'auto' or boolean\n", value);
		goto printError;
	    }
	    val = tmp;
	}
	break;
    case PSP_OP_FREEONSUSP:
    case PSP_OP_EXCLUSIVE:
    case PSP_OP_RUNJOBS:
    case PSP_OP_STARTER:
    case PSP_OP_PINPROCS:
    case PSP_OP_BINDMEM:
    case PSP_OP_BINDGPUS:
    case PSP_OP_BINDNICS:
    case PSP_OP_ALLOWUSERMAP:
    case PSP_OP_SUPPL_GRPS:
    case PSP_OP_RDPSTATISTICS:
    {
	int tmp, ret = parser_getBool(value, &tmp, NULL);
	if (ret==-1) {
	    printf("Illegal value '%s' is not boolean\n", value);
	    goto printError;
	}
	val = tmp;
	break;
    }
    case PSP_OP_NODESSORT:
	if (parser_parseString(value, &sortParser)) {
	    printf("Illegal value '%s'\n", value);
	    goto printError;
	}
	val = sortMode;
	break;
    case PSP_OP_CPUMAP:
	valList = cpusFromString(value);
	if (!valList) {
	    printf("Illegal value '%s' for CPU-map\n", value);
	    goto printError;
	}
	break;
    case PSP_OP_PLUGINAPIVERSION:
	printf("%s: pluginAPI is read only.\n", __func__);
	break;
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
	if (!strcasecmp(value, "unlimited")) {
	    val = RLIM_INFINITY;
	} else {
	    long tmp;
	    int ret = parser_getNumber(value, &tmp);
	    if (ret==-1) {
		printf("Illegal value '%s' is not int or 'unlimited'\n", value);
		goto printError;
	    }
	    val = tmp;
	}
	break;
    default:
	goto printError;
    }

    if (parser_getString()) goto printError;

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) goto error;
    }

    switch (setOpt) {
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
    case PSP_OP_RDPSTATISTICS:
    case PSP_OP_MCASTDEBUG:
    case PSP_OP_OVERBOOK:
    case PSP_OP_FREEONSUSP:
    case PSP_OP_EXCLUSIVE:
    case PSP_OP_RUNJOBS:
    case PSP_OP_STARTER:
    case PSP_OP_PINPROCS:
    case PSP_OP_BINDMEM:
    case PSP_OP_BINDGPUS:
    case PSP_OP_BINDNICS:
    case PSP_OP_ALLOWUSERMAP:
    case PSP_OP_NODESSORT:
    case PSP_OP_KILLDELAY:
    case PSP_OP_SUPPL_GRPS:
    case PSP_OP_MAXSTATTRY:
    case PSP_OP_MASTER:
    case PSP_OP_PLUGINUNLOADTMOUT:
    case PSP_OP_OBSOLETE:
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
	PSIADM_SetParam(setOpt, val, nl);
	break;
    case PSP_OP_CPUMAP:
	PSIADM_SetParamList(setOpt, valList, nl);
	break;
    default:
	goto printError;
    }

    if (valList) free(valList->value);
    free(valList);

    return 0;

printError:
    printError(&setInfo);

error:
    if (valList) free(valList->value);
    free(valList);

    return -1;
}

static int showCommand(char *token)
{
    char *what = parser_getString();
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

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
    case PSP_OP_RDPSTATISTICS:
    case PSP_OP_MCASTDEBUG:
    case PSP_OP_MASTER:
    case PSP_OP_FREEONSUSP:
    case PSP_OP_NODESSORT:
    case PSP_OP_OVERBOOK:
    case PSP_OP_EXCLUSIVE:
    case PSP_OP_STARTER:
    case PSP_OP_RUNJOBS:
    case PSP_OP_PINPROCS:
    case PSP_OP_BINDMEM:
    case PSP_OP_BINDGPUS:
    case PSP_OP_BINDNICS:
    case PSP_OP_ALLOWUSERMAP:
    case PSP_OP_KILLDELAY:
    case PSP_OP_SUPPL_GRPS:
    case PSP_OP_MAXSTATTRY:
    case PSP_OP_PLUGINAPIVERSION:
    case PSP_OP_PLUGINUNLOADTMOUT:
    case PSP_OP_OBSOLETE:
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
	} else {
	    printError(&testInfo);
	    return -1;
	}
    }

    PSIADM_TestNetwork(verbose);
    return 0;
}

/************************* plugin commands ******************************/

static int pluginLoadCmd(char *token)
{
    char *plugin = parser_getString();
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString() || !plugin) {
	printError(&pluginInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_Plugin(nl, plugin, PSP_PLUGIN_LOAD);
    return 0;
}

static int pluginRmCmd(char *token)
{
    char *plugin = parser_getString();
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString() || !plugin) {
	printError(&pluginInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_Plugin(nl, plugin, PSP_PLUGIN_REMOVE);
    return 0;
}

static int pluginFRmCmd(char *token)
{
    char *plugin = parser_getString();
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString() || !plugin) {
	printError(&pluginInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_Plugin(nl, plugin, PSP_PLUGIN_FORCEREMOVE);
    return 0;
}

static int pluginAvailCmd(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&pluginInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_PluginKey(nl, NULL, NULL, NULL, PSP_PLUGIN_AVAIL);
    return 0;
}

static int pluginHelpCmd(char *token)
{
    char *plugin = parser_getString();
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;
    char *key = NULL;

    if (nl_descr && !strcasecmp(nl_descr, "key")) {
	key = parser_getString();
	nl_descr = parser_getString();
    }

    if (parser_getString() || !plugin) {
	printError(&pluginInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_PluginKey(nl, plugin, key, NULL, PSP_PLUGIN_HELP);
    return 0;
}

static int pluginLoadTmCmd(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;
    char *plugin = NULL;

    if (nl_descr && !strcasecmp(nl_descr, "plugin")) {
	plugin = parser_getString();
	nl_descr = parser_getString();
    }

    if (parser_getString()) {
	printError(&pluginInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_PluginKey(nl, plugin, NULL, NULL, PSP_PLUGIN_LOADTIME);
    return 0;
}

static int pluginShowCmd(char *token)
{
    char *plugin = parser_getString();
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;
    char *key = NULL;

    if (nl_descr && !strcasecmp(nl_descr, "key")) {
	key = parser_getString();
	nl_descr = parser_getString();
    }

    if (parser_getString() || !plugin) {
	printError(&pluginInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_PluginKey(nl, plugin, key, NULL, PSP_PLUGIN_SHOW);
    return 0;
}

static int pluginSetCmd(char *token)
{
    char *plugin = parser_getString();
    char *key = parser_getString();
    char *value = parser_getQuotedString();
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString() || !plugin || !key || !value) {
	printError(&pluginInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_PluginKey(nl, plugin, key, value, PSP_PLUGIN_SET);
    return 0;
}

static int pluginUnsetCmd(char *token)
{
    char *plugin = parser_getString();
    char *key = parser_getString();
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString() || !plugin || !key) {
	printError(&pluginInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_PluginKey(nl, plugin, key, NULL, PSP_PLUGIN_UNSET);
    return 0;
}


static int pluginError(char *token)
{
    printError(&pluginInfo);
    return -1;
}

static keylist_t pluginList[] = {
    {"list", listPluginCommand, NULL},
    {"load", pluginLoadCmd, NULL},
    {"add", pluginLoadCmd, NULL},
    {"unload", pluginRmCmd, NULL},
    {"delete", pluginRmCmd, NULL},
    {"remove", pluginRmCmd, NULL},
    {"rm", pluginRmCmd, NULL},
    {"forceremove", pluginFRmCmd, NULL},
    {"forceunload", pluginFRmCmd, NULL},
    {"avail", pluginAvailCmd, NULL},
    {"help", pluginHelpCmd, NULL},
    {"show", pluginShowCmd, NULL},
    {"set", pluginSetCmd, NULL},
    {"unset", pluginUnsetCmd, NULL},
    {"loadtime", pluginLoadTmCmd, NULL},
    {NULL, pluginError, NULL}
};
static parser_t pluginParser = {" \t\n", pluginList};

static int pluginCommand(char *token)
{
    char *what = parser_getString();

    if (!what) listPluginCommand(what);

    return parser_parseString(what, &pluginParser);
}


/*************************** env commands *******************************/

static int envSetCommand(char *token)
{
    char *key = parser_getString();
    char *value = parser_getQuotedString();
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString() || !key || !value) {
	printError(&envInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_Environment(nl, key, value, PSP_ENV_SET);
    return 0;
}

static int envUnsetCommand(char *token)
{
    char *key = parser_getString();
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString() || !key) {
	printError(&envInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_Environment(nl, key, NULL, PSP_ENV_UNSET);
    return 0;
}

static int envError(char *token)
{
    printError(&envInfo);
    return -1;
}

static keylist_t envList[] = {
    {"list", listEnvCommand, NULL},
    {"set", envSetCommand, NULL},
    {"unset", envUnsetCommand, NULL},
    {"delete", envUnsetCommand, NULL},
    {NULL, envError, NULL}
};
static parser_t envParser = {" \t\n", envList};

static int envCommand(char *token)
{
    char *what = parser_getString();

    if (!what) listEnvCommand(what);

    return parser_parseString(what, &envParser);
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

static int helpParam(char *token)
{
    printInfo(&paramInfo);
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

static int helpEcho(char *token)
{
    printInfo(&echoInfo);
    return 0;
}

static int helpNodes(char *token)
{
    printInfo(&nodeInfo);
    return 0;
}

static int helpPlugin(char *token)
{
    printInfo(&pluginInfo);
    return 0;
}

static int helpEnv(char *token)
{
    printInfo(&envInfo);
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
    {"add", helpAdd, NULL},
    {"shutdown", helpShutdown, NULL},
    {"hwstart", helpHWStart, NULL},
    {"hwstop", helpHWStop, NULL},
    {"list", helpListCmd, NULL},
    {"status", helpListCmd, NULL},
    {"parameter", helpParam, NULL},
    {"range", helpRange, NULL},
    {"reset", helpReset, NULL},
    {"restart", helpRestart, NULL},
    {"version", helpVersion, NULL},
    {"exit", helpExit, NULL},
    {"quit", helpExit, NULL},
    {"set", helpSet, NULL},
    {"show", helpShow, NULL},
    {"kill", helpKill, NULL},
    {"test", helpTest, NULL},
    {"resolve", helpResolve, NULL},
    {"plugin", helpPlugin, NULL},
    {"environment", helpEnv, NULL},
    {"echo", helpEcho, NULL},
    {"sleep", helpSleep, NULL},
    {"nodes", helpNodes, NULL},
    {"help", helpHelp, NULL},
    {NULL, helpNotFound, NULL}
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

/************************** param commands ******************************/

static int paramSetCommand(char *token)
{
    char *key = parser_getString();
    char *value = parser_getQuotedString();

    if (parser_getString() || !key || !value) {
	printError(&paramInfo);
	return -1;
    }

    PSPARM_set(key, value);
    return 0;
}

static int paramShowCommand(char *token)
{
    char *key = parser_getString();

    if (parser_getString()) {
	printError(&paramInfo);
	return -1;
    }

    PSPARM_print(NULL, key);
    return 0;
}

static int paramHelpCommand(char *token)
{
    char *key = parser_getString();

    if (parser_getString()) {
	printError(&paramInfo);
	return -1;
    }

    PSPARM_printHelp(key);
    return 0;
}


static int paramError(char *token)
{
    printError(&paramInfo);
    return -1;
}

static keylist_t paramList[] = {
    {"set", paramSetCommand, NULL},
    {"show", paramShowCommand, NULL},
    {"help", paramHelpCommand, NULL},
    {NULL, paramError, NULL}
};
static parser_t paramParser = {" \t\n", paramList};

static int paramCommand(char *token)
{
    char *what = parser_getString();

    return parser_parseString(what, &paramParser);
}

/** Flag to print hostnames instead of PS IDs */
int paramHostname = 0;

static char * paramHostnamesHelp(void *data)
{
    return strdup("Flag to print hostnames instead of ParaStation IDs");
}


/** Flag to print hexadecimal values on some resource limits */
int paramHexFormat = 1;

static char * paramHexFormatHelp(void *data)
{
    return strdup("Flag to print hexadecimal values on some resource limits");
}

/** Delay (in ms) between consecutive starts of remote psids */
int paramStartDelay = 50;

static char * paramStartDelayHelp(void *data)
{
    return strdup("Delay (in ms) between consecutive starts of remote psids");
}

static char * setRange(void *data, char *nl_descr)
{
    if (!nl_descr) return strdup("value missing");

    bool *nl = getNodeList(nl_descr);
    if (nl) memcpy(defaultNL, nl, PSC_getNrOfNodes() * sizeof(*defaultNL));

    return NULL;
}

static char * paramRangeHelp(void *data)
{
    return strdup("The default range of nodes to act on");
}

static keylist_t *parametersList = NULL;

static void setupParameters(void)
{
    PSPARM_init();

    PSPARM_register("range", NULL,
		    &setRange, &printRange, paramRangeHelp, NULL);
    PSPARM_register("hostnames", &paramHostname,
		    PSPARM_boolSet, PSPARM_boolPrint, paramHostnamesHelp,
		    PSPARM_boolKeys);
    PSPARM_register("hexformat", &paramHexFormat,
		    PSPARM_boolSet, PSPARM_boolPrint, paramHexFormatHelp,
		    PSPARM_boolKeys);
    PSPARM_register("startdelay", &paramStartDelay,
		    PSPARM_uintSet, PSPARM_intPrint, paramStartDelayHelp, NULL);

    parametersList = PSPARM_getKeylist();
    for (int i = 0; paramList[i].key; i++) paramList[i].next = parametersList;
}

static void cleanupParameters(void)
{
    if (parametersList) PSPARM_freeKeylist(parametersList);
    parametersList = NULL;

    for (int i = 0; paramList[i].key; i++) paramList[i].next = NULL;

    PSPARM_finalize();
}


/************************************************************************/

static int versionCommand(char *token)
{
    if (parser_getString()) {
	printError(&versionInfo);
	return -1;
    }

    printf("PSIADMIN: ParaStation administration tool\n");
    printf(" Copyright (C) 1996-2004 ParTec AG, Karlsruhe\n");
    printf(" Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH,"
	   " Munich\n");
    printf(" Copyright (C) 2021-2024 ParTec AG, Munich\n");
    printf("\n");
    printf("PSProtocol: %d\t(with a %sgeneous setup)\n", PSProtocolVersion,
	   PSI_mixedProto() ? "hetero" : "homo");
    printf("PSIADMIN:   %s\n", PSC_getVersionStr());

    char tmp[100];
    int err = PSI_infoString(-1, PSP_INFO_RPMREV, NULL, tmp, sizeof(tmp), false);
    if (err) strcpy(tmp, "unknown");
    printf("PSID:       %s\n", tmp);
    return 0;
}

static int echoCommand(char *token)
{
    char *line = parser_getLine();

    printf("%s\n", line ? line : "");
    return 0;
}

static int sleepCommand(char *token)
{
    long tmp;

    token = parser_getString();
    if (!token
	|| parser_getNumber(token, &tmp) || tmp < 0
	|| parser_getString()) {
	printError(&sleepInfo);
	return -1;
    }

    sleep(tmp);
    return 0;
}

static int resolveCommand(char *token)
{
    char *nl_descr = parser_getString();
    bool *nl = defaultNL;

    if (parser_getString()) {
	printError(&resolveInfo);
	return -1;
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);
	if (!nl) return -1;
    }

    PSIADM_Resolve(nl);
    return 0;
}

/** Magic value returned by the parser function to show 'quit' was reached. */
#define quitMagic 17

static int quitCommand(char *token)
{
    if (parser_getString()) {
	printError(&exitInfo);
	return -1;
    }

    return quitMagic;
}

static int error(char *token)
{
    printf("Syntax error\n");

    return -1;
}

static keylist_t commandList[] = {
    {"add", addCommand, NULL},
    {"shutdown", shutdownCommand, NULL},
    {"hwstart", hwstartCommand, NULL},
    {"hwstop", hwstopCommand, NULL},
    {"range", rangeCommand, NULL},
    {"restart", restartCommand, NULL},
    {"reset", resetCommand, NULL},
    {"list", listCommand, listList},
    {"status", listCommand, listList},
    {"show", showCommand, setShowList},
    {"set", setCommand, setShowList},
    {"kill", killCommand, NULL},
    {"test", testCommand, NULL},
    {"?", helpCommand, NULL},
    {"help", helpCommand, helpList},
    {"version", versionCommand, NULL},
    {"sleep", sleepCommand, NULL},
    {"echo", echoCommand, NULL},
    {"resolve", resolveCommand, NULL},
    {"plugin", pluginCommand, pluginList},
    {"environment", envCommand, envList},
    {"parameter", paramCommand, paramList},
    {"exit", quitCommand, NULL},
    {"quit", quitCommand, NULL},
    {NULL, error, NULL}
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
    {NULL, parseCommand, NULL}
};
static parser_t lineParser = {";\n", lineList};

bool parseLine(char *line)
{
    char *token;

    /* Put line into strtok() */
    token = parser_registerString(line, &lineParser);

    /* Do the parsing */
    return (parser_parseString(token, &lineParser) == quitMagic);
}

void parserPrepare(void)
{
    parser_init(0, NULL);
    parser_setDebugMask(0);
    setupDefaultNL();
    setupParameters();
}

void parserRelease(void)
{
    cleanupParameters();
    releaseDefaultNL();
    parser_finalize();
}

void completeLine(const char *buf, linenoiseCompletions *lc)
{
    keylist_t *genList = commandList;
    int tokStart = 0, tokEnd;
    int end = strlen(buf);
    int start = end;
    while (start > 0 && !isspace(buf[start-1])) start--;

    /* Try to find a token in front of the text to complete */
    while (buf[tokStart] && isspace(buf[tokStart])) tokStart++;

    if (tokStart != start) {
	bool paramOpts = true;
	genList = commandList;
	while (tokStart != start) {
	    char *token, *matchedToken;

	    tokEnd = tokStart;
	    while (buf[tokEnd] && !isspace(buf[tokEnd])) tokEnd++;
	    if (!buf[tokEnd]) {
		printf("return to save libedit\n");
		return; /* prevent libedit to segfault */
	    }

	    token = strndup(&buf[tokStart], tokEnd-tokStart);

	    genList = parser_nextKeylist(token, genList, &matchedToken);
	    if (genList == setShowList && !strcmp(matchedToken, "show"))
		paramOpts = false;
	    if (genList == parametersList) {
		paramOpts = false;
		if (!strcmp(matchedToken, "set")) paramOpts = true;
	    }

	    free(token);
	    tokStart = tokEnd+1;
	    while (buf[tokStart] && isspace(buf[tokStart])) tokStart++;
	}
	if (!paramOpts &&
	    (genList == PSPARM_boolKeys || genList == boolList
	     || genList == sortList )) {
	    genList = NULL;
	}
    }

    if (!genList) return;

    const char *name, *text = &buf[start];
    char completion[1024];
    size_t len = strlen(text);
    int index = 0;

    if (start > 0) memcpy(completion, buf, start);

    while ((name = genList[index].key)) {
	if (!strncmp(name, text, len)) {
	    memcpy(completion+start, name, strlen(name)+1);
	    linenoiseAddCompletion(lc, completion);
	}
	index++;
    }
}
