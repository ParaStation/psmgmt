/*
 *               ParaStation
 * helpmsgs.c
 *
 * Help messages of the ParaStation adminstration tool
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: helpmsgs.c,v 1.9 2004/01/27 21:04:19 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: helpmsgs.c,v 1.9 2004/01/27 21:04:19 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>

/**
 * Structure holding a syntax information. The content is intended to
 * be put out via @ref printSyntax(). Refer to @ref printSyntax() for
 * the output format of this kind of information.
 */
typedef struct {
    char *cmd;            /**< The command to describe. */
    char *arg;            /**< The possible arguments to the command. */
} syntax_t;

/**
 * Structure holding tagged information. The content is intended to
 * be put out via @ref printTag(). Refer to @ref printTag() for
 * the output format of this kind of information.
 */
typedef struct {
    char *tag;            /**< The tag marking the information. */
    char *descr;          /**< The actual information. */
} taggedInfo_t;

/** Structure holding complete information on commands */
typedef struct {
    char *head;           /**< Optional header. Will get underline */
    syntax_t *syntax;     /**< Command's syntax. */
    int nodes;            /**< Flag to mark nodes info to be printed. */
    char *descr;          /**< General description of the command. */
    taggedInfo_t *tags;   /**< Further tagged info. Usually describing
			     further arguments to the command. */
    char *comment;        /**< Trailing comment. Currently ignored. */
} info_t;

static info_t helpInfo = {
    .head = "ParaStation Admin: available commands:",
    .syntax = NULL,
    .nodes = 0,
    .descr = NULL,
    .tags = (taggedInfo_t[]) {
	{ .tag = "add",
	  .descr = "Start the ParaStation daemon process on some or all nodes."
	},
	{ .tag = "kill",
	  .descr = "Terminate a ParaStation process on any node." },
	{ .tag = "show",
	  .descr = "Show control parameters." },
	{ .tag = "status",
	  .descr = "Status information." },
	{ .tag = "version",
	  .descr = "Print version numbers." },
	{ .tag = "exit",
	  .descr = "Same as quit." },
	{ .tag = "quit",
	  .descr = "Quit PSIadmin." },
	{ NULL, NULL}
    },
    .comment = NULL
};

static info_t privilegedInfo = {
    .head = "Privileged commands:",
    .syntax = NULL,
    .nodes = 0,
    .descr = NULL,
    .tags = (taggedInfo_t[]) {
	{ .tag = "reset",
	  .descr = "Reset the daemons or network." },
	{ .tag = "restart",
	  .descr = "Restart ParaStation nodes." },
	{ .tag = "set",
	  .descr = "Alter control parameters." },
	{ .tag = "shutdown",
	  .descr = "Shutdown the ParaStation daemon process on some or all"
	  " nodes." },
	{ .tag = "test",
	  .descr = "The the ParaStation network." },
	{ NULL, NULL }
    },
    .comment = NULL
};

static info_t nodeInfo = {
    .head = NULL,
    .syntax = NULL,
    .nodes = 0,
    .descr = NULL,
    .tags = (taggedInfo_t[]) {
	{ .tag = "<nodes> ",
	  .descr = "selects one or more ranges of nodes. <nodes> is of the"
	  " form s1[-e1]{,si[-ei]}*, where the s and e are positiv numbers"
	  " representing ParaStation IDs. Each comma-separated part of"
	  " <nodes> denotes a range of nodes. If a range's '-e' part is"
	  " missing, it represents a single node. In principle <nodes> might"
	  " contain an unlimited number of ranges. If <nodes> is empty, all"
	  " nodes of the ParaStation cluster are selected. As an extension"
	  " <nodes> might also be a hostname that can be resolved into a"
	  " valid ParaStation ID." },
	{ NULL, NULL }
    },
    .comment = NULL
};

static info_t addInfo = {
    .head = "Add command:",
    .syntax = (syntax_t[]) {{
	.cmd = "add",
	.arg = "<nodes>"
    }},
    .nodes = 1,
    .descr = "Add the selected nodes to the cluster by starting the"
    " ParaStation daemon processes (psid) on the respective nodes.",
    .tags = NULL,
    .comment = NULL
};

static info_t shutdownInfo = {
    .head = "Shutdown command (privileged):",
    .syntax = (syntax_t[]) {{
	.cmd = "shutdown",
	.arg = "<nodes>"
    }},
    .nodes = 1,
    .descr = "Shutdown the ParaStation daemon on all selected nodes. As a"
    " consequence ALL processes using the selected nodess are terminated"
    " (killed)!",
    .tags = NULL,
    .comment = NULL
};

static info_t killInfo = {
    .head = "Kill command:",
    .syntax = (syntax_t[]) {{
	.cmd = "kill",
	.arg = "[-<sig>] <tid>"
    }},
    .nodes = 0,
    .descr = "Send the signal <sig> to the process with the given task ID"
    " <tid>. The task ID of a process might be obtained with the help of the"
    " 'status proc' command. The default signal sent is SIGTERM.",
    .tags = NULL,
    .comment = NULL
};

static info_t hwstartInfo = {
    .head = "HWStart command:",
    .syntax = (syntax_t[]) {{
	.cmd = "hwstart",
	.arg = "[hw {<hw> | all}] <nodes>"
    }},
    .nodes = 1,
    .descr = "Start the declared hardware on the selected nodes. Starting a"
    " specific hardware will be tried on the selected nodes regardless if"
    " this hardware is specified for this nodes within the 'parastation.conf'"
    " file. On the other hand, if 'hw all' is specified or the 'hw' option is"
    " missing at all, all the specified hardwaretypes are started.",
    .tags = NULL,
    .comment = NULL
};

static info_t hwstopInfo = {
    .head = "HWStop command:",
    .syntax = (syntax_t[]) {{
	.cmd = "hwstop",
	.arg = "[hw {<hw> | all}] <nodes>"
    }},
    .nodes = 1,
    .descr = "Stop the declared hardware on the selected nodes. If 'hw all' is"
    " specified or the 'hw' option is missing at all, all hardware is"
    " stopped.",
    .tags = NULL,
    .comment = NULL
};

static info_t setInfo = {
    .head " Set command:",
    .syntax = (syntax_t[]) {{
	.cmd = "set",
	.arg = "{maxproc {<num>|any} | user {<user>|any} | group {<group>|any}"
	" | psiddebug <level> | rdpdebug <level> | rdppktloss <rate> "
	" | rdpmaxretrans <val> | mcastdebug <level> | smallpacketsize <size>"
	" | hnpend <val> | ackpend <val>} <nodes>"
    }},
    .nodes = 1,
    .descr = "Set one of various parameters of the ParaStation system:",
    .tags = (taggedInfo_t[]) {
	{ .tag = "set maxproc {<num>|any}",
	  .descr = "Set the maximum number of ParaStation processes. If the"
	"argument is 'any', an unlimited number of processes is allowed." },
	{ .tag = "set user {<user>|any}",
	  .descr = "Grant access to a particular or any user. <user> might be"
	  "a user name or a numerical UID." },
	{ .tag = "set group {<group>|any}",
	  .descr = "Grant access to a particular or any group. <group> might"
	  " be a group name or a numerical GID." },
	{ .tag = "set psiddebug <level>",
	  .descr = "Set the ParaStation daemon's verbosity level to <level> on"
	  " the selected nodes."
	  " Depending on <level> the daemon might log a huge amount of"
	  " messages to the syslog. Thus do not use large values for <level>"
	  " for a long time." },
	{ .tag = "set rdpdebug <level>",
	  .descr = "Set RDP protocol's debugging level to <level> on the"
	  " seleceted nodes."
	  " Depending on <level> the daemon might log a huge amount of"
	  " messages to the syslog. Thus do not use large values for <level>"
	  " for a long time." },
	{ .tag = "set rdppktloss <rate>",
	  .descr = "Set RDP protocol's paket loss to <rate> on the seleceted"
	  " nodes. <rate> is given in percent and therefore in between 0 and"
	  " 100. This options is for debugging purposes only and may break"
	  " connections between ParaStation daemons" },
	{ .tag = "set rdpmaxretrans <val>",
	  .descr = "Set RDP protocol's maximum retransmission count." },
	{ .tag = "set mcastdebug <level>",
	  .descr = "Set MCast facility's debugging level to <level> on the"
	  " seleceted nodes. Depending on <level> the daemon might log a huge"
	  " amount of messages to the syslog. Thus do not use large values"
	  " for <level> for a long time." },
	{ .tag = "set smallpacketsize <size>",
	  .descr = "Set MCP's maximum size of PIO packets to <size> bytes." },
	{ .tag = "set ackpend <val>",
	  .descr = "Set MCP's ACKPend parameter to <val>." },
	{ .tag = "set hnpend <val>",
	  .descr = "Set MCP's HNPend parameter to <val>." },
	{ NULL, NULL }
    },
    .comment = "For more information reffer to 'help set <subcommand>'"
};

static info_t showInfo = {
    .head = "Show command:",
    .syntax = (syntax_t[]) {{
	.cmd = "show",
	.arg = "{maxproc | user | group | psiddebug | rdpdebug | rdppktloss"
	" | rdpmaxretrans | mcastdebug | master | smallpacketsize"
	" | resendtimeout | hnpend | ackpend} <nodes>"
    }},
    .nodes = 1,
    .descr = "Show various parameters of the ParaStation system:",
    .tags = (taggedInfo_t[]) {
	{ .tag = "show maxproc",
	  .descr = "Show maximum number of ParaStation processes." },
	{ .tag = "show user",
	  .descr = "Show user access is granted to." },
	{ .tag = "show group",
	  .descr = "Show group access is granted to." },
	{ .tag = "show psiddebug",
	  .descr = "Show daemons verbosity level." },
	{ .tag = "show rdpdebug",
	  .descr = "Show RDP protocol's verbosity level." },
	{ .tag = "show rdppktloss",
	  .descr = "Show RDP protocol's packet-loss rate." },
	{ .tag = "show rdpmaxretrans",
	  .descr = "Show RDP protocol's maximum retransmission count." },
	{ .tag = "show mcastdebug",
	  .descr = "Show MCast facility's verbosity level." },
	{ .tag = "show master",
	  .descr = "Show master handling all the partition requests." },
	{ .tag = "show smallpacketsize",
	  .descr = "Show MCP's maximum size of PIO packets in bytes." },
	{ .tag = "show resendtimeout",
	  .descr = "Show MCP's resend timeout in microseconds." },
	{ .tag = "show hnpend",
	  .descr = "Show MCP's HNPend parameter." },
	{ .tag = "show ackpend",
	  .descr = "Show MCP's AckPend parameter." },
	{ NULL, NULL }
    },
    .comment = NULL
};

static info_t statInfo = {
    .head = "Status command:",
    .syntax = (syntax_t[]) {{
	.cmd = "s[tatus]",
	.arg = "{[node] | c[ount] [hw <hw>] | p[roc] [cnt <cnt>]"
	" | {allproc|ap} [cnt <cnt>] | {hardware|hw} | l[oad] | rdp"
	" | mcast} <nodes>"
    }},
    .nodes = 1,
    .descr = "Show various status parameters of the ParaStation system:",
    .tags = (taggedInfo_t[]) {
	{ .tag = "status [node]",
	  .descr = "shows the active nodes amongst the selected ones." },
	{ .tag = "status c[ount] [hw <hw>]",
	  .descr = "Show the hardware counters on the selected nodes. If 'hw"
	  " <hw>' is given, only the counters of the specified hardware are"
	  " displayed. The possible values of <hw> can be found out using the"
	  " 'status hw' command." },
	{ .tag = "status p[roc] [cnt <cnt>]",
	  .descr = "Show processes managed by ParaStation on the selected"
	  " nodes. Only normal processes are displayed, no forwarder, spawner"
	  " etc. processes. Up to <cnt> processes per node will be displayed."
	  " The default is to show 10 processes."},
	{ .tag = "status {allproc|ap} [cnt <cnt>]",
	  .descr = "Show all processes managed by ParaStation on the selected"
	  " nodes. This includes all special processes like forwarder, spawner"
	  " etc. Up to <cnt> processes per node will be displayed. The default"
	  " is to show 10 processes."},
	{ .tag = "status {hardware|hw}",
	  .descr = "Show the available communcation hardware on the selected"
	  " nodes." },
	{ .tag = "status l[oad]",
	  .descr = "Show the load on the selected nodes." },
	{ .tag = "status rdp",
	  .descr = "Show the status of the RDP protocol on the selected"
	  " nodes." },
	{ .tag = "status mcast",
	  .descr = "Show the status of the MCast facility on the selected"
	  " nodes." },
	{ NULL, NULL }
    },
    .comment = NULL
};

static info_t versionInfo = {
    .head = "Version command:",
    .syntax = (syntax_t[]) {{
	.cmd = "v[ersion]",
	.arg = ""
    }},
    .nodes = 0,
    .descr = "Prints various version numbers.",
    .tags = NULL,
    .comment = NULL
};

static info_t exitInfo = {
    .head = "Exit command:",
    .syntax = (syntax_t[]) {{
	.cmd = "{e[xit] | q[uit]}",
	.arg = ""
    }},
    .nodes = 0,
    .descr = "Exit the ParaStation administration tool.",
    .tags = NULL,
    .comment = NULL
};

static info_t resetInfo = {
    .head = "Reset command (privileged):",
    .syntax = (syntax_t[]) {{
	.cmd = "reset",
	.arg = "[hw] <nodes>"
    }},
    .nodes = 1,
    .descr = "Reset the ParaStation daemon on the selected nodes. If 'hw' is"
    " given, this includes the reinitialization of the communication hardware"
    " managed by ParaStation. In any case as a consequence, ALL processes"
    " managed by ParaStation on the selected nodes are terminated (killed)!"
    " The 'reset hw' command is an alias to 'restart'.",
    .tags = NULL,
    .comment = NULL
};

static info_t restartInfo = {
    .head = "Restart command (privileged):",
    .syntax = (syntax_t[]) {{
	.cmd = "restart",
	.arg = "<nodes>"
    }},
    .nodes = 1,
    .descr = "Restart the ParaStation daemon on the selected nodes. This"
    " includes reinitialization of the communication hardware managed by"
    " ParaStation. Thus the selected part of the ParaStation cluster is forced"
    " to reinitialized. As a consequence, ALL processes managed by ParaStation"
    " on the selected nodes are terminated (killed)! This command is an alias"
    " to 'reset hw'.",
    .tags = NULL,
    .comment = NULL
};

static info_t testInfo = {
    .head = "Test command (privileged):",
    .syntax = (syntax_t[]) {{
	.cmd = "test",
	.arg = "{quiet | [normal] | verbose}"
    }},
    .nodes = 0,
    .descr = "All communications links in a ParaStation network are tested.",
    .tags = (taggedInfo_t[]) {
	{ .tag = "quiet",
	  .descr = "just a ok is told on success." },
	{ .tag = "normal",
	  .descr = "only the coordinator node is telling about his activity."
	},
	{ .tag = "verbose",
	  .descr = "each node is telling about his activity." }
    },
    .comment = NULL
};

/* ---------------------------------------------------------------------- */

/** A long string of separating characters */
static const char sep[] =   "================================================"
"============================================================================";

/** A long string of whitespace characters */
static const char space[] = "                                                "
"                                                                            ";

/**
 * @brief Get screen width.
 *
 * Get the screen width of the terminal stdout is connected to.
 *
 * If the TIOCGWINSZ @ref ioctl() is available, it is used to
 * determine the width. Otherwise the COLUMNS environment variable is
 * used to identify the size.
 *
 * If the determined width is smaller than 60, it is set to this
 * minimum value.
 *
 * If both methods cited above failed, the width is set to the default
 * size of 80.
 *
 *
 * @return On success, the actual screen size is returned. If the
 * determination of the current screen size failed, the default width
 * 80 is passed to the calling function. If the determined width is
 * too small, the minimal width 60 is returned.
 *
 * @see ioctl()
 */
static int getWidth(void)
{
    int width = 0;
    char *ss;
#if defined (TIOCGWINSZ)
    struct winsize window_size;

    if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &window_size) == 0) {
	width = (int) window_size.ws_col;
    }
#endif /* TIOCGWINSZ */

    if (width <= 0) {
	char *colstr = getenv("COLUMNS");
	if (colstr) width = atoi(colstr);
    }

    /* Everything failed. Use standard width */
    if (width < 1) width = 80;
    /* Extend to minimum width */
    if (width < 60) width = 60;

    return width;
}

/**
 * @brief Print syntax information.
 *
 * Print syntax information provided in @a syntax after displaying the
 * leading tag @a tag. The syntax_t structure @a syntax consists of
 * two part, the actual command and trailing arguments.
 *
 * The output format is as follows: After the indenting tag @a tag, at
 * first the command is given out. This is followed by the arguments.
 *
 * If the output generated in this way does not fit within one line,
 * it is wrapped at suitable positions of the trailing arguments. For
 * this purpose an indentation of the length of the leading tag and
 * the actual command is taken into account. Thus, the leading tag and
 * the command part of the syntax are expected to be (much) smaller
 * than the length of the actual line.
 *
 * Suitable positions for a line wrap are withspace (' ') characters
 * which are not preceeded by pipe ('|') characters, or the
 * corresponding pipe character. Leading whitespace at the beginning
 * of a wrapped line - apart from the indentation - will be skipped.
 *
 * @param tag The indenting tag of the command syntax to print.
 *
 * @param syntax The actual command syntax to print.
 *
 * @return No return value.
 */
static void printSyntax(const char *tag, syntax_t *syntax)
{
    int lwidth = getWidth() - strlen(tag);

    if (syntax->cmd) {
	lwidth -= strlen(syntax->cmd) + 1;
	printf("%s%s ", tag, syntax->cmd);
    }
    if (syntax->arg) {
	char *pos = syntax->arg;
	int len = strlen(pos);

	while (len>lwidth) {
	    char *end = pos + lwidth;
	    while (*end != ' ') end--;
	    if (*(end-1) == '|') end --; /* Don't end with '|' */
	    printf("%.*s\n", (int)(end-pos), pos);
	    printf("%.*s", getWidth()-lwidth, space);
	    pos = end;
	    while (*pos == ' ') pos++; /* skip leading whitespace */
	    len = strlen(pos);
	}
	printf("%.*s\n", lwidth, pos);
    }
    return;
}

/**
 * @brief Print tagged description.
 *
 * Print the description @a descr preceeded by the tag @a tag.
 *
 * The output format is as follows: After the indenting tag @a tag,
 * the description is printed out.
 *
 * If the output generated does not fit within one line, it is wrapped
 * at suitable positions. For this purpose an indentation of the
 * length of the leading tag is taken into account. Thus, the leading
 * tag is expected to be (much) smaller than the length of the actual
 * line.
 *
 * Suitable positions for a line wrap are withspace (' ') characters.
 * Leading whitespace at the beginning of a wrapped line - apart from
 * the indentation - will be skipped.
 *
 * @param tag The indenting tag of the description to print.
 *
 * @param descr The actual description of the tag to print.
 *
 * @return No return value.
 */
static void printDescr(const char *tag, char *descr)
{
    int lwidth = getWidth() - strlen(tag);

    if (descr) {
	char *pos = descr;
	int len = strlen(pos);

	printf("%s", tag);
	while (len>lwidth) {
	    char *end = pos + lwidth - 1;
	    while (*end != ' ') end--;
	    printf("%.*s\n", (int)(end-pos), pos);
	    printf("%.*s", getWidth()-lwidth, space);
	    pos = end+1;             /* Ignore the separating space */
	    len = strlen(pos);
	}
	printf("%.*s\n", lwidth, pos);
    }
    return;
}

/**
 * @brief Print tagged info.
 *
 * Print tagged info provided within @a tags. The taggedInfo_t
 * structure tags consists of pairs of tags and descriptions to this
 * tag.
 *
 * In order to create the output, first of all the maximum length of
 * the actual tags within @a tag is determined. Then each pair of tag
 * and description is printed to stdout using the @ref printDescr()
 * function, where the actual tag is embedded within a string filled
 * up with whitespace to reach the maximum taglength. This yields to
 * the effect, that each description starts at the same
 * column. i.e. all descriptions are equally indented.
 * 
 * @return No return value.
 *
 * @see printDescr()
 */
static void printTags(taggedInfo_t *tags)
{
    unsigned int t, tagwidth = 0;
    char *tag;

    if (!tags) return;

    for (t=0; tags[t].tag; t++)
	if (strlen(tags[t].tag) > tagwidth) tagwidth = strlen(tags[t].tag);

    tag = malloc(tagwidth+4);
    for (t=0; tags[t].tag; t++) {
	sprintf(tag, " %*s  ", tagwidth, tags[t].tag);
	printDescr(tag, tags[t].descr);
    }
    free(tag);
}

/**
 * @brief Print error.
 *
 * Print a syntax error message followed by the correct syntax of the
 * command detected. The correct syntax is taken from @a info.
 *
 * In order to do the actual output, @ref printSyntax() might be
 * called. If no syntax is defined within @a info, a general warning
 * is created.
 *
 * @param info Structure holding the information to print out.
 *
 * @return No return value.
 *
 * @see printSyntax()
 */
static void printError(info_t *info)
{
    if (!info) {
	printf("%s: No info given\n", __func__);
	return;
    }

    if (info->syntax) {
	printSyntax("Syntax error: ", info->syntax);
    } else {
	printf("%s: No syntax available\n", __func__);
    }

    return;
}

/**
 * @brief Print info.
 *
 * Print complete content of the structure @a info holding it. The
 * output depends on the content of the structure. E.g. if a head is
 * defined, it will be printed, but it's no error for @a info to hold
 * no header.
 *
 * In order to do the actual output, further functions like @ref
 * printSyntax(), @ref printInfo(), @ref printDescr() or @ref
 * printTags() might be called.
 *
 * @param info Structure holding the information to print out.
 *
 * @return No return value.
 *
 * @see printSyntax(), printInfo(), printDescr(), printTags()
 */
static void printInfo(info_t *info)
{
    if (!info) return;

    if (info->head) {
	int len = strlen(info->head);

	if (len) {
	    printf("\n%s\n", info->head);
	    if (len < getWidth()) printf("%.*s\n", len, sep);
	    printf("\n");
	}
    }

    if (info->syntax) {
	printSyntax("Syntax: ", info->syntax);
	printf("\n");
    }
    if (info->nodes) printInfo(&nodeInfo);
    if (info->descr) {
	printDescr("Description: ", info->descr);
	printf("\n");
    }
    if (info->tags) {
	printTags(info->tags);
	printf("\n");
    }

    return;
}
