/*
 *               ParaStation
 * adminparser.c
 *
 * ParaStation admin command line parser functions
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: adminparser.c,v 1.8 2004/01/09 15:55:59 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char lexid[] __attribute__(( unused )) = "$Id: adminparser.c,v 1.8 2004/01/09 15:55:59 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static char parserversion[] = "$Revision: 1.8 $";

static char *getNodeList(char *nl_descr)
{
    static char *nl = NULL;
    char *tmp = strdup(nl_descr);
    char *ret = PSC_parseNodelist(tmp);

    free(tmp);
    if (ret) return ret;

    {
	PSnodes_ID_t node;
	struct hostent *hp = gethostbyname(nl_descr);
	struct sockaddr_in sa;
	int err;

	if (!hp) return NULL;

	memcpy(&sa.sin_addr, *hp->h_addr_list, sizeof(sa.sin_addr));
	err = PSI_infoNodeID(-1, PSP_INFO_HOST, &sa.sin_addr.s_addr, &node, 1);
	
	if (err){
	    printf("Illegal nodename %s\n", nl_descr);
	    return NULL;
	}

	nl = realloc(nl, PSC_getNrOfNodes());
	memset(nl, 0, PSC_getNrOfNodes());

	nl[node] = 1;
	return nl;
    }
}

static int addCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = NULL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) goto error;
    }

    PSIADM_AddNode(nl);
    return 0;

 error:
    printf("Syntax error: add [<nodes>]\n");
    return -1;
}

static int shutdownCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = NULL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) goto error;
    }

    PSIADM_ShutdownNode(nl);
    return 0;

 error:
    printf("Syntax error: shutdown [<nodes>]\n");
    return -1;
}

static int startCommand(char *token)
{
    /** @todo */
    printf("Not implemented\n");
    return 0;
}

static int stopCommand(char *token)
{
    /** @todo */
    printf("Not implemented\n");
    return 0;
}

static int hwstartCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = NULL, *hw = NULL;
    int hwIndex = -1;

    if (nl_descr && !strcasecmp(nl_descr, "hw")) {
	hw = parser_getString();
	nl_descr = parser_getString();
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) goto error;
    }

    if (parser_getString()) goto error;

    if (hw) {
	int err = PSI_infoInt(-1, PSP_INFO_HWINDEX, hw, &hwIndex, 1);
	if (err || (hwIndex == -1 && strcasecmp(hw, "all"))) goto error;
    }
    PSIADM_HWStart(hwIndex, nl);
    return 0;

 error:
    printf ("Syntax error: hwstart [hw {<hw> | all}] [<nodes>]\n");
    return -1;
}

static int hwstopCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = NULL, *hw = NULL;
    int hwIndex = -1;

    if (nl_descr && !strcasecmp(nl_descr, "hw")) {
	hw = parser_getString();
	nl_descr = parser_getString();
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) goto error;
    }

    if (parser_getString()) goto error;

    if (hw) {
	int err = PSI_infoInt(-1, PSP_INFO_HWINDEX, hw, &hwIndex, 1);
	if (err || (hwIndex == -1 && strcasecmp(hw, "all"))) goto error;
    }
    PSIADM_HWStop(hwIndex, nl);
    return 0;

 error:
    printf ("Syntax error: hwstop [hw {<hw> | all}] [<nodes>]\n");
    return -1;
}

static int restartCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = NULL;

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) goto error;
    }

    PSIADM_Reset(1, nl);
    return 0;

 error:
    printf("Syntax error: restart [<nodes>]\n");
    return -1;
}

static int resetCommand(char *token)
{
    char *nl_descr = parser_getString();
    char *nl = NULL;
    int hw = 0;

    if (nl_descr && !strcasecmp(nl_descr, "hw")) {
	nl_descr = parser_getString();
	hw = 1;
    }

    if (parser_getString()) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) goto error;
    }

    PSIADM_Reset(hw, nl);
    return 0;

 error:
    printf("Syntax error: reset [hw] [<nodes>]\n");
    return -1;
}

static int statCommand(char *token)
{
    char *what = parser_getString();
    char *nl_descr = parser_getString();
    char *nl = NULL, *hw = NULL;
    int cnt = 10;

    if (what && (!strcasecmp(what, "count")
		 || !strcasecmp(what, "c"))) {

	if (nl_descr && !strcasecmp(nl_descr, "hw")) {
	    hw = parser_getString();
	    nl_descr = parser_getString();
	}
    } else if (what && (!strcasecmp(what, "allproc")
			|| !strcasecmp(what, "ap")
			|| !strcasecmp(what, "proc")
			|| !strcasecmp(what, "p"))) {
	if (nl_descr && !strcasecmp(nl_descr, "cnt")) {
	    char *tok = parser_getString();
	    cnt = parser_getNumber(tok);
	    nl_descr = parser_getString();
	}
    }

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) goto error;
    }

    if (parser_getString()) goto error;

    if (!what || !strcasecmp(what, "node")) {
	PSIADM_NodeStat(nl);
    } else if (!strcasecmp(what, "count")
	       || !strcasecmp(what, "c")) {
	int hwIndex = -1;
	if (hw) {
	    int err = PSI_infoInt(-1, PSP_INFO_HWINDEX, hw, &hwIndex, 1);
	    if (err || hwIndex == -1) goto error;
	}
	PSIADM_CountStat(hwIndex, nl);
    } else if (!strcasecmp(what, "rdp")) {
	PSIADM_RDPStat(nl);
    } else if (!strcasecmp(what, "mcast")) {
	PSIADM_MCastStat(nl);
    } else if (!strcasecmp(what, "proc")
	       || !strcasecmp(what, "p")) {
	PSIADM_ProcStat(cnt, 0, nl);
    } else if (!strcasecmp(what, "allproc")
	       || !strcasecmp(what, "ap")) {
	PSIADM_ProcStat(cnt, 1, nl);
    } else if (!strcasecmp(what, "load")
	       || !strcasecmp(what, "l")) {
	PSIADM_LoadStat(nl);
    } else if (!strcasecmp(what, "hardware")
	       ||!strcasecmp(what, "hw")) {
	PSIADM_HWStat(nl);
    } else if (!strcasecmp(what, "all")) {
	PSIADM_NodeStat(nl);
	PSIADM_ProcStat(cnt, 0, nl);
    } else if (!nl_descr) {
	/* Maybe this is like 'status a-b' */
	nl = getNodeList(what);

	if (!nl) goto error;
	PSIADM_NodeStat(nl);
    } else goto error;
    return 0;

 error:
    printf ("Syntax error: s[tatus] <what> [<nodes>]\n");
    return -1;
}

static long procsFromString(char *procs)
{
    long tmp = parser_getNumber(procs);

    if (strcasecmp(procs, "any") == 0) return -1;
    if (tmp > -1) return tmp;

    return -2;
}

static uid_t uidFromString(char *user)
{
    long tmp = parser_getNumber(user);
    struct passwd *passwd = getpwnam(user);

    if (strcasecmp(user, "any") == 0) return -1;
    if (tmp > -1) return tmp;
    if (passwd) return passwd->pw_uid;
    
    printf("Unknown user %s\n", user);
    return -2;
}

static gid_t gidFromString(char *group)
{
    long tmp = parser_getNumber(group);
    struct group *grp = getgrnam(group);

    if (strcasecmp(group, "any") == 0) return -1;
    if (tmp > -1) return tmp;
    if (grp) return grp->gr_gid;
    
    printf("Unknown group %s\n", group);
    return -2;
}

static int setCommand(char *token)
{
    char *what = parser_getString();
    char *value = parser_getString();
    char *nl_descr = parser_getString();
    char *nl = NULL;
    PSP_Option_t option = 0;
    long val;

    if (parser_getString() || !what || !value) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) goto error;
    }

    if (strcasecmp(what, "maxproc") == 0) {
	option = PSP_OP_PROCLIMIT;
	val = procsFromString(value);
	if (val == -2) goto error;
    } else if (strcasecmp(what, "user") == 0) {
	option = PSP_OP_UIDLIMIT;
	val = uidFromString(value);
	if (val == -2) goto error;
    } else if (strcasecmp(what, "group") == 0) {
	option = PSP_OP_GIDLIMIT;
	val = gidFromString(value);
	if (val == -2) goto error;
    } else {
	if (strcasecmp(what, "psiddebug") == 0) {
	    option = PSP_OP_PSIDDEBUG;
	} else if (strcasecmp(what, "selecttime") == 0) {
	    option = PSP_OP_PSIDSELECTTIME;
	} else if (strcasecmp(what, "rdpdebug") == 0) {
	    option = PSP_OP_RDPDEBUG;
	} else if (strcasecmp(what, "rdppktloss") == 0) {
	    option = PSP_OP_RDPPKTLOSS;
	} else if (strcasecmp(what, "rdpmaxretrans") == 0) {
	    option = PSP_OP_RDPMAXRETRANS;
	} else if (strcasecmp(what, "mcastdebug") == 0) {
	    option = PSP_OP_MCASTDEBUG;
	} else if (strcasecmp(what, "smallpacketsize") == 0) {
	    option = PSP_OP_PSM_SPS;
	} else if (strcasecmp(what, "sps") == 0) {
	    option = PSP_OP_PSM_SPS;
	} else if (strcasecmp(what, "resendtimeout") == 0) {
	    option = PSP_OP_PSM_RTO;
	} else if (strcasecmp(what, "rto") == 0) {
	    option = PSP_OP_PSM_RTO;
	} else if (strcasecmp(what, "hnpend") == 0) {
	    option = PSP_OP_PSM_HNPEND;
	} else if (strcasecmp(what, "ackpend") == 0) {
	    option = PSP_OP_PSM_ACKPEND;
	} else goto error;

	val = parser_getNumber(value);
	if (val==-1) {
	    printf("Illegal value %s\n", value);
	    goto error;
	}
    }
    PSIADM_SetParam(option, val, nl);
    return 0;

 error:
    printf ("Syntax error: set <item> <value> [<nodes>]\n");
    return -1;
}

static int showCommand(char *token)
{
    char *what = parser_getString();
    char *nl_descr = parser_getString();
    char *nl = NULL;
    PSP_Option_t option = 0;

    if (parser_getString() || !what) goto error;

    if (nl_descr) {
	nl = getNodeList(nl_descr);

	if (!nl) goto error;
    }

    if (!strcasecmp(what, "maxproc")) {
	option = PSP_OP_PROCLIMIT;
    } else if (!strcasecmp(what, "user")) {
	option = PSP_OP_UIDLIMIT;
    } else if (!strcasecmp(what, "group")) {
	option = PSP_OP_GIDLIMIT;
    } else if (!strcasecmp(what, "psiddebug")) {
	option = PSP_OP_PSIDDEBUG;
    } else if (!strcasecmp(what, "selecttime")) {
	option = PSP_OP_PSIDSELECTTIME;
    } else if (!strcasecmp(what, "rdpdebug")) {
	option = PSP_OP_RDPDEBUG;
    } else if (!strcasecmp(what, "rdppktloss")) {
	option = PSP_OP_RDPPKTLOSS;
    } else if (!strcasecmp(what, "rdpmaxretrans")) {
	option = PSP_OP_RDPMAXRETRANS;
    } else if (!strcasecmp(what, "mcastdebug")) {
	option = PSP_OP_MCASTDEBUG;
    } else if (!strcasecmp(what, "smallpacketsize")) {
	option = PSP_OP_PSM_SPS;
    } else if (!strcasecmp(what, "sps")) {
	option = PSP_OP_PSM_SPS;
    } else if (!strcasecmp(what, "resendtimeout")) {
	option = PSP_OP_PSM_RTO;
    } else if (!strcasecmp(what, "rto")) {
	option = PSP_OP_PSM_RTO;
    } else if (!strcasecmp(what, "hnpend")) {
	option = PSP_OP_PSM_HNPEND;
    } else if (!strcasecmp(what, "ackpend")) {
	option = PSP_OP_PSM_ACKPEND;
    } else goto error;

    PSIADM_ShowParam(option, nl);
    return 0;

 error:
    printf ("Syntax error: show <item> [<nodes>]\n");
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
    printf ("Syntax error: kill [-signal] tid\n");
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
    printf ("Syntax error: test {normal|verbose|quiet}\n");
    return -1;
}

static int helpCommand(char *token)
{
    char *option = parser_getString();

    if (option) {
	if (!strcasecmp(option, "add")) {
	    printInfo(&addInfo);
	} else if (!strcasecmp(option, "shutdown")) {
	    printInfo(&shutdownInfo);
	} else if (!strcasecmp(option, "start")) {
	    printf("Not yet implemented\n");
	} else if (!strcasecmp(option, "stop")) {
	    printf("Not yet implemented\n");
	} else if (!strcasecmp(option, "hwstart")) {
	    printInfo(&hwstartInfo);
	} else if (!strcasecmp(option, "hwstop")) {
	    printInfo(&hwstopInfo);
	} else if (!strcasecmp(option, "status")
		   || !strcasecmp(option, "stat")
		   || !strcasecmp(option, "s")) {
	    printInfo(&statInfo);
	} else if (!strcasecmp(option, "reset")) {
	    printInfo(&resetInfo);
	} else if (!strcasecmp(option, "restart")) {
	    printInfo(&restartInfo);
	} else if (!strcasecmp(option, "v")
		   || !strcasecmp(option, "version")) {
	    printInfo(&versionInfo);
	} else if (!strcasecmp(option, "exit")
		   || !strcasecmp(option, "e")
		   || !strcasecmp(option, "quit")
		   || !strcasecmp(option, "q")) {
	    printInfo(&exitInfo);
	} else if (!strcasecmp(option, "set")) {
	    printInfo(&setInfo);
	} else if (!strcasecmp(option, "show")) {
	    printInfo(&showInfo);
	} else if (!strcasecmp(option, "kill")) {
	    printInfo(&killInfo);
	} else if (!strcasecmp(option, "test")) {
	    printInfo(&testInfo);
	} else if (!strcasecmp(option, "nodes")) {
	    printInfo(&nodeInfo);
	} else goto error;
    } else {
	printInfo(&helpInfo);
	if (!getuid()) printInfo(&privilegedInfo);
	printf("For more information type 'help <command>'\n\n");
    }

    while (parser_getString());

    return 0;

 error:
    printf ("Syntax error: help [<command>]\n");
    return -1;
}

static int versionCommand(char *token)
{
    extern char psiadmversion[], commandversion[];

    if (parser_getString()) goto error;
    
    printf("PSIADMIN: ParaStation administration tool\n");
    printf("Copyright (C) 1996-2004 ParTec AG Karlsruhe\n");
    printf("\n");
    printf("PSIADMIN:   %s\b/ %s\b/ %s\b \b\b\n", psiadmversion+11,
	   commandsversion+11, parserversion+11);
    printf("PSID:       %s\b \n", PSI_getPsidVersion()+11);
    printf("PSProtocol: %d\n", PSprotocolVersion);
    return 0;

 error:
    printf ("Syntax error: v[ersion]\n");
    return -1;
}

/** Magic value returned by the parser function to show 'quit' was reached. */
#define quitMagic 17

static int quitCommand(char *token)
{
    if (parser_getString()) goto error;
    return quitMagic;

 error:
    printf ("Syntax error: {e[xit]|q[uit]}\n");
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
    {"start", startCommand},
    {"stop", stopCommand},
    {"hwstart", hwstartCommand},
    {"hwstop", hwstopCommand},
    {"restart", restartCommand},
    {"reset", resetCommand},
    {"status", statCommand},
    {"stat", statCommand},
    {"s", statCommand},
    {"show", showCommand},
    {"set", setCommand},
    {"kill", killCommand},
    {"test", testCommand},
    {"h", helpCommand},
    {"?", helpCommand},
    {"help", helpCommand},
    {"v", versionCommand},
    {"version", versionCommand},
    {"exit", quitCommand},
    {"quit", quitCommand},
    {"q", quitCommand},
    {"e", quitCommand},
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
	parser_setDebugLevel(0);
	firstCall = 0;
    }

    /* Put line into strtok() */
    token = parser_registerString(line, &lineParser);

    /* Do the parsing */
    return (parser_parseString(token, &lineParser) == quitMagic);
}
