/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Help messages of the ParaStation Administration Tool
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pscommon.h"

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
    .syntax = (syntax_t[]) {{
	.cmd = "help",
	.arg = "{ add | echo | environment | help | hwstart | hwstop | kill"
	" | list | parameter | plugin | range | reset | resolve | restart"
	" | set | show | shutdown | sleep | test | version | quit }"
    }},
    .nodes = 0,
    .descr = NULL,
    .tags = (taggedInfo_t[]) {
	{ .tag = "echo",
	  .descr = "Echo remainder of the line given." },
	{ .tag = "help",
	  .descr = "Give help to distinct directives." },
	{ .tag = "kill",
	  .descr = "Terminate processes controlled by ParaStation on any node."
	},
	{ .tag = "list",
	  .descr = "List information." },
	{ .tag = "parameter",
	  .descr = "Handle psiadmin's control parameters." },
	{ .tag = "range",
	  .descr = "Set or show the default node-range." },
	{ .tag = "resolve",
	  .descr = "Resolve hostname to nodeID mapping." },
	{ .tag = "show",
	  .descr = "Show the daemon's control parameters." },
	{ .tag = "sleep",
	  .descr = "Sleep for a given period." },
	{ .tag = "version",
	  .descr = "Print version numbers." },
	{ .tag = "quit",
	  .descr = "Quit PSIadmin." },
	{ .tag = "exit",
	  .descr = "Same as quit." },
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
	{ .tag = "add",
	  .descr = "Start the ParaStation daemon process on some or all nodes."
	},
	{ .tag = "environment",
	  .descr = "List and modify the ParaStation daemon's environment" },
	{ .tag = "hwstart",
	  .descr = "Enable communication channels." },
	{ .tag = "hwstop",
	  .descr = "Disable communication channels." },
	{ .tag = "plugin",
	  .descr = "Handle ParaStation daemon plugins." },
	{ .tag = "reset",
	  .descr = "Reset the daemons or network." },
	{ .tag = "restart",
	  .descr = "Restart ParaStation nodes." },
	{ .tag = "set",
	  .descr = "Alter the daemon's control parameters." },
	{ .tag = "shutdown",
	  .descr = "Shutdown the ParaStation daemon process on some or all"
	  " nodes." },
	{ .tag = "test",
	  .descr = "Test the ParaStation network." },
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
	  .descr = "selects one or more ranges of nodes. <nodes> is either of"
	  " the form s1[-e1]{,si[-ei]}*, where the s and e are positive numbers"
	  " representing ParaStation IDs, or 'all'. Each comma-separated part"
	  " of <nodes> denotes a range of nodes. If a range's '-e' part is"
	  " missing, it represents a single node. In principle <nodes> might"
	  " contain an unlimited number of ranges. If <nodes> value is 'all',"
	  " all nodes of the ParaStation cluster are selected. If <nodes> is"
	  " empty, the node range preselected via the 'range' command is used."
	  " The default preselected node range contains all nodes of the"
	  " ParaStation cluster. As an extension <nodes> might also be a"
	  " comma-separated list of hostnames that can be resolved into a"
	  " valid ParaStation ID." },
	{ NULL, NULL }
    },
    .comment = NULL
};

static info_t rangeInfo = {
    .head = "Range command:",
    .syntax = (syntax_t[]) {{
	.cmd = "range",
	.arg = "[<nodes> | all]"
    }},
    .nodes = 0,
    .descr = "Preselect or display the default range of nodes used within"
    " the commands of the ParaStation admin. If <nodes> is given, the default"
    " range of nodes is set. Otherwise the actual default range of nodes is"
    " displayed. <nodes> is of the form s1[-e1]{,si[-ei]}*, where the s and e"
    " are positive numbers representing ParaStation IDs. Each comma-separated"
    " part of <nodes> denotes a range of nodes. If a range's '-e' part is"
    " missing, it represents a single node. In principle <nodes> might contain"
    " an unlimited number of ranges.",
    .tags = NULL,
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
	.arg = "[silent] <nodes>"
    }},
    .nodes = 1,
    .descr = "Shutdown the ParaStation daemon on all selected nodes. As a"
    " consequence ALL processes using the selected nodes are terminated"
    " (killed)! If the 'silent' flag is given, no warning concerning nodes"
    " already down is printed.",
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
    " missing at all, all the specified hardware-types are started.",
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
    .head = " Set command:",
    .syntax = (syntax_t[]) {{
	.cmd = "set",
	.arg = "{maxproc {<num>|any} | user [+|-]{<user>|any}"
	" | group [+|-]{{<group>|any} | psiddebug <level> | master <id>"
	" | selecttime <timeout> | statustimeout <timeout>"
	" | statusbroadcasts <limit> | deadlimit <limit>"
	" | rdpdebug <level> | rdptimeout <timeout> | rdppktloss <rate>"
	" | rdpmaxretrans <val> | rdpresendtimeout <val>| rdpretrans <val> "
	" | rdpclosedtimeout <val> | rdpmaxackpend <val> | rdpstatistics <bool>"
	" | mcastdebug <level> | {freeonsuspend|fos} <bool>"
	" | starter <bool> | runjobs <bool>"
	" | overbook {<bool>|auto} | exclusive <bool> | pinprocs <bool>"
	" | bindmem <bool> | supplementaryGroups <bool> | maxStatTry <num>"
	" | cpumap <map> | allowUserMap <bool> | nodessort <mode>"
	" | adminuser [+|-]{<user>|any} | admingroup [+|-]{<group>|any}"
	" | accountpoll <interval> | killdelay <delay>"
	" | pluginUnloadTmout <timeout> | obsoleteTasks 0"
	" | {rl_{addressspace|as} | rl_core | rl_cpu | rl_data | rl_fsize"
	"    | rl_locks | rl_memlock | rl_msgqueue | rl_nofile | rl_nproc"
	"    | rl_rss | rl_sigpending | rl_stack} {<limit>|unlimited}} <nodes>"
    }},
    .nodes = 1,
    .descr = "Set one of various parameters of the ParaStation system:",
    .tags = (taggedInfo_t[]) {
	{ .tag = "set maxproc {<num>|any}",
	  .descr = "Set the maximum number of ParaStation processes. If the"
	  "argument is 'any', an unlimited number of processes is allowed." },
	{ .tag = "set user [+|-]{<user>|any}",
	  .descr = "Grant access to a particular or any user. <user> might be"
	  " a user name or a numerical UID. If <user> is preceded by a '+' or"
	  " '-', this user is added to or removed from the list of users"
	  " respectively." },
	{ .tag = "set group [+|-]{<group>|any}",
	  .descr = "Grant access to a particular or any group. <group> might"
	  " be a group name or a numerical GID. If <group> is preceded by a"
	  " '+' or '-', this group is added to or removed from the list of"
	  " groups respectively." },
	{ .tag = "set psiddebug <level>",
	  .descr = "Set the ParaStation daemon's verbosity level to <level> on"
	  " the selected nodes."
	  " Depending on <level> the daemon might log a huge amount of"
	  " messages to the syslog. Thus do not use large values for <level>"
	  " for a long time." },
	{ .tag = "set master <id>",
	  .descr = "Give the ParaStation daemon's some hints concerning the"
	  " master node. This will actually trigger the daemon to connect the"
	  " node with ParaStation ID <id>."},
	{ .tag = "set selecttime <timeout>",
	  .descr = "Set the ParaStation daemon's select timeout to <timeout>"
	  " seconds on the selected nodes." },
	{ .tag = "set statustimeout <timeout>",
	  .descr = "Set the ParaStation daemon's status timeout to <timeout>"
	  " milli-seconds on the selected nodes." },
	{ .tag = "set statusbroadcasts <limit>",
	  .descr = "Set the ParaStation daemon's limit on status-broadcasts"
	  " to <limit> on the selected nodes." },
	{ .tag = "set deadlimit <limit>",
	  .descr = "Set the ParaStation daemon's dead-limit to <limit> on the"
	  " selected nodes." },
	{ .tag = "set rdpdebug <level>",
	  .descr = "Set RDP protocol's debugging level to <level> on the"
	  " selected nodes."
	  " Depending on <level> the daemon might log a huge amount of"
	  " messages to the syslog. Thus do not use large values for <level>"
	  " for a long time." },
	{ .tag = "set rdptimeout <timeout>",
	  .descr = "Set the RDP's central timeout to <timeout> milli-seconds"
	  " on the selected nodes. This will modify the actual timer registered"
	  " by RDP. This should be smaller than RDP's resend-timeout." },
	{ .tag = "set rdppktloss <rate>",
	  .descr = "Set RDP protocol's packet loss to <rate> on the selected"
	  " nodes. <rate> is given in percent and therefore in between 0 and"
	  " 100. This options is for debugging purposes only and may break"
	  " connections between ParaStation daemons" },
	{ .tag = "set rdpmaxretrans <val>",
	  .descr = "Set RDP protocol's maximum retransmission count." },
	{ .tag = "set rdpresendtimeout <val>",
	  .descr = "Set RDP protocol's resend timeout in milli-seconds." },
	{ .tag = "set rdpretrans <val>",
	  .descr = "Set RDP protocol's total retransmission count. Most"
	  " probably you want to reset this to 0." },
	{ .tag = "set rdpclosedtimeout <val>",
	  .descr = "Set RDP protocol's closed timeout in milli-seconds." },
	{ .tag = "set rdpmaxackpend <val>",
	  .descr = "Set RDP protocol's maximum pending ACK count." },
	{ .tag = "set rdpstatistics <bool>",
	  .descr = "Flag RDP statistics. This includes total number of packets"
	  " sent and mean time to ACK." },
	{ .tag = "set mcastdebug <level>",
	  .descr = "Set MCast facility's debugging level to <level> on the"
	  " selected nodes. Depending on <level> the daemon might log a huge"
	  " amount of messages to the syslog. Thus do not use large values"
	  " for <level> for a long time." },
	{ .tag = "set {freeonsuspend|fos} <bool>",
	  .descr = "Set flag marking if resources of suspended jobs are freed"
	  " temporarily to <bool>. Relevant values are 'false', 'true', 'no',"
	  " 'yes', 0 or different from 0."
	  " Only the value on the master node really steers the behavior!" },
	{ .tag = "set starter <bool>",
	  .descr = "Set flag marking if starting is allowed from this nodes"
	  " to <bool>. Relevant values are 'false', 'true', 'no',"
	  " 'yes', 0 or different from 0." },
	{ .tag = "set runjobs <bool>",
	  .descr = "Set flag marking if this nodes run processes"
	  " to <bool>. Relevant values are 'false', 'true', 'no',"
	  " 'yes', 0 or different from 0." },
	{ .tag = "set overbook {<bool>|auto}",
	  .descr = "Set flag marking if this nodes shall be overbooked upon"
	  " user-request. Relevant values are 'auto', 'false', 'true',"
	  " 'no', 'yes', 0 or different from 0." },
	{ .tag = "set exclusive <bool>",
	  .descr = "Set flag marking if this nodes can be requested by users"
	  " exclusively to <bool>. Relevant values are 'false', 'true',"
	  " 'no', 'yes', 0 or different from 0." },
	{ .tag = "set pinprocs <bool>",
	  .descr = "Set flag marking if this nodes will use process-pinning"
	  " to bind processes to cores. Relevant values are 'false', 'true',"
	  " 'no', 'yes', 0 or different from 0." },
	{ .tag = "set bindmem <bool>",
	  .descr = "Set flag marking if this nodes will use memory-binding"
	  " as NUMA policy. Relevant values are 'false', 'true',"
	  " 'no', 'yes', 0 or different from 0." },
	{ .tag = "set supplementaryGroups <bool>",
	  .descr = "Set flag marking if this nodes will set the user's"
	  " supplementary groups while spawning new processes. Relevant"
	  " values are 'false', 'true', 'no', 'yes', 0 or different from 0." },
	{ .tag = "set maxStatTry <num>",
	  .descr = "Set the maximum number of tries to stat() an executable"
	  " while spawning new processes to <num>. All numbers larger than 0"
	  " are allowed." },
	{ .tag = "set cpumap <map>",
	  .descr = "Set the map used to assign CPU-slots to physical cores"
	  " to <map>. <map> is a quoted string containing a space-separated"
	  " permutation of the number 0 to <Ncore>-1. Here <Ncore> is the"
	  " number of physical cores available on this node. The number of"
	  " cores within a distinct node may be determined via 'list hw'."
	  " The first number in <map> is the number of the physical core the"
	  " first CPU-slot will be mapped to, and so on." },
	{ .tag = "set allowUserMap <bool>",
	  .descr = "Set flag marking if this nodes will allow user to influence"
	  " the mapping of processes to physical core. Relevant values are"
	  " 'false', 'true', 'no', 'yes', 0 or different from 0." },
	{ .tag = "set nodessort <mode>",
	  .descr = "Define the default sorting strategy for nodes when"
	  " attaching them to a partition. Valid values for <mode> are"
	  " PROC, LOAD1, LOAD5, LOAD15, PROC+LOAD or NONE. This only comes"
	  " into play, if the user does not define a sorting strategy"
	  " explicitly via PSI_NODES_SORT. Be aware of the fact that using"
	  " a batch-system like PBS or LSF *will* set the strategy"
	  " explicitly, namely to NONE." },
	{ .tag = "set adminuser [+|-]{<user>|any}",
	  .descr = "Grant authorization to start admin-tasks, i.e. task not"
	  " accounted, to a particular or any user. <user> might be a user"
	  " name or a numerical UID. If <user> is preceded by a '+' or '-',"
	  " this user is added to or removed from the list of adminusers"
	  " respectively." },
	{ .tag = "set admingroup [+|-]{<group>|any}",
	  .descr = "Grant authorization to start admin-tasks, i.e. task not"
	  " accounted, to a particular or any group. <group> might be a group"
	  " name or a numerical GID. If <group> is preceded by a '+' or '-',"
	  " this group is added to or removed from the list of admingroups"
	  " respectively." },
	{ .tag = "set accountpoll <interval>",
	  .descr = "In order to retrieve more detailed accounting information"
	  " the forwarders might poll on /proc. This sets the poll interval on"
	  " the selected nodes to <interval> seconds. If Set to 0, no polling"
	  " at all will take place." },
	{ .tag = "set killdelay <delay>",
	  .descr = "Unexpectedly dying relatives (i.e. the parent process or"
	  " any child process) will lead to a signal to be sent. The processes"
	  " is expected to finalize after receiving such signal. In order to"
	  " enforce this, a SIGKILL signal is sent with some delay. This sets"
	  " the delay on the selected node to <delay> seconds. If set to  0,"
	  " no SIGKILL at all will be sent." },
	{ .tag = "set pluginUnloadTmout <timeout>",
	  .descr = "Set the timeout until plugins are evicted after a"
	  " 'plugin forceunload' to <timeout> seconds." },
	{ .tag = "set obsoleteTasks <num>",
	  .descr = "Reset the number of obsolete tasks. Makes most sense to"
	  " reset it to 0 which also clears the list of obsolete tasks." },
	{ .tag = "set rl_{addressspace|as} {<limit>|unlimited}",
	  .descr = "Set RLIMIT_AS to the given <limit> for both,"
	  " rlim_cur and rlim_max." },
	{ .tag = "set rl_core {<limit>|unlimited}",
	  .descr = "Set RLIMIT_CORE to the given <limit> for both,"
	  " rlim_cur and rlim_max." },
	{ .tag = "set rl_cpu {<limit>|unlimited}",
	  .descr = "Set RLIMIT_CPU to the given <limit> for both,"
	  " rlim_cur and rlim_max." },
	{ .tag = "set rl_data {<limit>|unlimited}",
	  .descr = "Set RLIMIT_DATA to the given <limit> for both,"
	  " rlim_cur and rlim_max." },
	{ .tag = "set rl_fsize {<limit>|unlimited}",
	  .descr = "Set RLIMIT_FSIZE to the given <limit> for both,"
	  " rlim_cur and rlim_max." },
	{ .tag = "set rl_locks {<limit>|unlimited}",
	  .descr = "Set RLIMIT_LOCKS to the given <limit> for both,"
	  "rlim_cur and rlim_max." },
	{ .tag = "set rl_memlock {<limit>|unlimited}",
	  .descr = "Set RLIMIT_MEMLOCK to the given <limit> for both,"
	  " rlim_cur and rlim_max." },
	{ .tag = "set rl_msgqueue {<limit>|unlimited}",
	  .descr = "Set RLIMIT_MSGQUEUE to the given <limit> for both,"
	  " rlim_cur and rlim_max." },
	{ .tag = "set rl_nofile {<limit>|unlimited}",
	  .descr = "Set RLIMIT_NOFILE to the given <limit> for both,"
	  " rlim_cur and rlim_max." },
	{ .tag = "set rl_nproc {<limit>|unlimited}",
	  .descr = "Set RLIMIT_NPROC to the given <limit> for both,"
	  " rlim_cur and rlim_max." },
	{ .tag = "set rl_rss {<limit>|unlimited}",
	  .descr = "Set RLIMIT_RSS to the given <limit> for both,"
	  " rlim_cur and rlim_max." },
	{ .tag = "set rl_sigpending {<limit>|unlimited}",
	  .descr = "Set RLIMIT_SIGPENDING to the given <limit> for both,"
	  " rlim_cur and rlim_max." },
	{ .tag = "set rl_stack {<limit>|unlimited}",
	  .descr = "Set RLIMIT_STACK to the given <limit> for both,"
	  " rlim_cur and rlim_max." },
	{ NULL, NULL }
    },
    .comment = "For more information refer to 'help set <subcommand>'"
};

static info_t showInfo = {
    .head = "Show command:",
    .syntax = (syntax_t[]) {{
	.cmd = "show",
	.arg = "{maxproc | user | group | psiddebug | selecttime"
	" | statustimeout | statusbroadcasts | deadlimit | rdpdebug"
	" | rdptimeout | rdppktloss | rdpmaxretrans | rdpresendtimeout"
	" | rdpretrans | rdpclosedtimeout | rdpmaxackpend | rdpstatistics"
	" | mcastdebug | master | {freeonsuspend|fos}"
	" | starter | runjobs | overbook | exclusive | pinprocs | bindmem"
	" | cpumap | allowUserMap | nodessort | supplementaryGroups"
	" | maxStatTry | adminuser | admingroup | accounters | accountpoll"
	" | killdelay | pluginAPIversion | pluginUnloadTmout | obsoleteTasks"
	" | rl_{addressspace|as} | rl_core | rl_cpu | rl_data | rl_fsize"
	" | rl_locks | rl_memlock | rl_msgqueue | rl_nofile | rl_nproc"
	" | rl_rss | rl_sigpending | rl_stack}"
	" <nodes>"
    }},
    .nodes = 1,
    .descr = "Show various parameters of the ParaStation system:",
    .tags = (taggedInfo_t[]) {
	{ .tag = "show maxproc",
	  .descr = "Show maximum number of ParaStation processes." },
	{ .tag = "show user",
	  .descr = "Show users access is granted to." },
	{ .tag = "show group",
	  .descr = "Show groups access is granted to." },
	{ .tag = "show psiddebug",
	  .descr = "Show daemon's verbosity level." },
	{ .tag = "show selecttime",
	  .descr = "Show daemon's select timeout." },
	{ .tag = "show statustimeout",
	  .descr = "Show daemon's status timeout." },
	{ .tag = "show statusbroadcasts",
	  .descr = "Show daemon's limit on status-broadcasts." },
	{ .tag = "show deadlimit",
	  .descr = "Show daemon's dead-limit." },
	{ .tag = "show rdpdebug",
	  .descr = "Show RDP protocol's verbosity level." },
	{ .tag = "show rdptimeout",
	  .descr = "Show RDP protocol's registered timer's timeout." },
	{ .tag = "show rdppktloss",
	  .descr = "Show RDP protocol's packet-loss rate." },
	{ .tag = "show rdpmaxretrans",
	  .descr = "Show RDP protocol's maximum retransmission count." },
	{ .tag = "show rdpresendtimeout",
	  .descr = "Show RDP protocol's resend timeout in milli-seconds." },
	{ .tag = "show rdpretrans",
	  .descr = "Show RDP protocol's total retransmission count." },
	{ .tag = "show rdpclosedtimeout",
	  .descr = "Show RDP protocol's closed timeout in milli-seconds." },
	{ .tag = "show rdpmaxackpend",
	  .descr = "Show RDP protocol's maximum pending ACK count." },
	{ .tag = "show rdpstatistics",
	  .descr = "Show if RDP statistics are taken." },
	{ .tag = "show mcastdebug",
	  .descr = "Show MCast facility's verbosity level." },
	{ .tag = "show master",
	  .descr = "Show master handling all the partition requests." },
	{ .tag = "show {freeonsuspend|fos}",
	  .descr = "Show flag marking if resources of suspended jobs are freed"
	  " temporarily. Only the value on the master node really steers the"
	  " behaviour!" },
	{ .tag = "show starter",
	  .descr = "Show flag marking if starting is allowed." },
	{ .tag = "show runjobs",
	  .descr = "Show flag marking if this nodes run processes" },
	{ .tag = "show overbook",
	  .descr = "Show flag marking if this nodes shall be overbooked upon"
	  " user-request." },
	{ .tag = "show exclusive",
	  .descr = "Show flag marking if this nodes can be requested by users"
	  " exclusively." },
	{ .tag = "show pinproc",
	  .descr = "Show flag marking if this nodes uses process pinning." },
	{ .tag = "show bindmem",
	  .descr = "Show flag marking if this nodes uses binding as NUMA"
	  " policy." },
	{ .tag = "show supplementaryGroups",
	  .descr = "Show flag marking if this nodes will set the user's"
	  " supplementary groups while spawning new processes." },
	{ .tag = "show maxStatTry",
	  .descr = "Show the maximum number of tries to stat() an executable"
	  " while spawning new processes to <num>." },
	{ .tag = "show cpumap",
	  .descr = "Show the map assigning CPU-slots to physical cores on"
	  " this node." },
	{ .tag = "show allowUserMap",
	  .descr = "Show flag marking if this nodes will allow user to"
	  " influence the mapping of processes to physical core." },
	{ .tag = "show nodessort",
	  .descr = "Show the default sorting strategy used when attaching"
	  " nodes to partitions." },
	{ .tag = "show adminuser",
	  .descr = "Show users allowed to start admin-tasks, i.e. unaccounted"
	  " tasks." },
	{ .tag = "show admingroup",
	  .descr = "Show groups allowed to start admin-tasks, i.e. unaccounted"
	  " tasks." },
	{ .tag = "show accounters",
	  .descr = "Show all accounter tasks, i.e. tasks collecting accounting"
	  " messages." },
	{ .tag = "show accountpoll",
	  .descr = "Show polling interval of accounter to retrieve more"
	  " detailed information. Value is in seconds." },
	{ .tag = "show killdelay",
	  .descr = "Show delay before sending SIGKILL from relatives. Value"
	  " is in seconds."},
	{ .tag = "show pluginAPIversion",
	  .descr = "Show the version number of the plugin API." },
	{ .tag = "show pluginUnloadTmout",
	  .descr = "Show the timeout until plugins are evicted after a"
	  " 'plugin forceunload'." },
	{ .tag = "show obsoleteTasks",
	  .descr = "Show the number of obsolete tasks on this node." },
	{ .tag = "show rl_{addressspace|as}",
	  .descr = "Show RLIMIT_AS on this node." },
	{ .tag = "show rl_core",
	  .descr = "Show RLIMIT_CORE on this node." },
	{ .tag = "show rl_cpu",
	  .descr = "Show RLIMIT_CPU on this node." },
	{ .tag = "show rl_data",
	  .descr = "Show RLIMIT_DATA on this node." },
	{ .tag = "show rl_fsize",
	  .descr = "Show RLIMIT_FSIZE on this node." },
	{ .tag = "show rl_locks",
	  .descr = "Show RLIMIT_LOCKS on this node." },
	{ .tag = "show rl_memlock",
	  .descr = "Show RLIMIT_MEMLOCK on this node." },
	{ .tag = "show rl_msgqueue",
	  .descr = "Show RLIMIT_MSGQUEUE on this node." },
	{ .tag = "show rl_nofile",
	  .descr = "Show RLIMIT_NOFILE on this node." },
	{ .tag = "show rl_nproc",
	  .descr = "Show RLIMIT_NPROC on this node." },
	{ .tag = "show rl_rss",
	  .descr = "Show RLIMIT_RSS on this node." },
	{ .tag = "show rl_sigpending",
	  .descr = "Show RLIMIT_SIGPENDING on this node." },
	{ .tag = "show rl_stack",
	  .descr = "Show RLIMIT_STACK on this node." },
	{ NULL, NULL }
    },
    .comment = NULL
};

static info_t listInfo = {
    .head = "List command:",
    .syntax = (syntax_t[]) {{
	.cmd = "list",
	.arg = "{{[node] | count [hw <hw>] | {p|processes} [cnt <cnt>]"
	" | {aproc|allprocesses} [cnt <cnt>] | environment [key <key>]"
	" | {hardware|hw} | down | up | load | rdp | rdpconnection | mcast"
	" | plugins | summary [max <max>] | starttime | startupscript"
	" | nodeupscript | nodedownscript | instdir | versions} <nodes> |"
	" jobs [state {r[unning] | p[ending] | s[uspended]}] [slots] [<tid>]}"
    }},
    .nodes = 1,
    .descr = "Show various status parameters of the ParaStation system:",
    .tags = (taggedInfo_t[]) {
	{ .tag = "list [node]",
	  .descr = "shows the active nodes amongst the selected ones." },
	{ .tag = "list count [hw <hw>]",
	  .descr = "Show the hardware counters on the selected nodes. If 'hw"
	  " <hw>' is given, only the counters of the specified hardware are"
	  " displayed. The possible values of <hw> can be found out using the"
	  " 'list hw' command." },
	{ .tag = "list proc [cnt <cnt>]",
	  .descr = "Show processes managed by ParaStation on the selected"
	  " nodes. Only normal processes are displayed, no forwarder, spawner"
	  " etc. processes. Up to <cnt> processes per node will be displayed."
	  " The default is to show 10 processes."},
	{ .tag = "list aproc [cnt <cnt>]",
	  .descr = "Show all processes managed by ParaStation on the selected"
	  " nodes. This includes all special processes like forwarder, spawner"
	  " etc. Up to <cnt> processes per node will be displayed. The default"
	  " is to show 10 processes."},
	{ .tag = "list environment [key <key>]",
	  .descr = "Show the environment on the selected nodes. If 'key <key>'"
	  " is given, only the environment variable <key> and its value is"
	  " displayed." },
	{ .tag = "list {hardware|hw}",
	  .descr = "Show the available communication hardware on the selected"
	  " nodes." },
	{ .tag = "list down",
	  .descr = "List hostnames of down nodes within the selected ones." },
	{ .tag = "list up",
	  .descr = "List hostnames of up nodes within the selected ones." },
	{ .tag = "list load",
	  .descr = "Show the load on the selected nodes." },
	{ .tag = "list rdp",
	  .descr = "Show the status of the RDP protocol on the selected"
	  " nodes." },
	{ .tag = "list rdpconnection",
	  .descr = "Show info on RDP connections on the selected nodes." },
	{ .tag = "list mcast",
	  .descr = "Show the status of the MCast facility on the selected"
	  " nodes." },
	{ .tag = "list memory",
	  .descr = "Show total / free memory for the selected nodes." },
	{ .tag = "list plugins",
	  .descr = "Show the currently loaded plugins on the selected nodes." },
	{ .tag = "list summary [max <max>]",
	  .descr = "Print a brief summary of the active and down nodes. If less"
	  " than <max> nodes are down, a range specification of these nodes is"
	  " printed, too." },
	{ .tag = "list starttime",
	  .descr = "Show the daemon's start-time on the selected nodes." },
	{ .tag = "list startupscript",
	  .descr = "Show the daemon's script called during startup in order to"
	  " test the local situation on the selected nodes." },
	{ .tag = "list nodeupscript",
	  .descr = "Show the daemon's script called on the master node whenever"
	  " a daemon connects after being down before on the selected nodes." },
	{ .tag = "list nodedownscript",
	  .descr = "Show the daemon's script called on the master node whenever"
	  " a daemon disconnects on the selected nodes." },
	{ .tag = "list instdir",
	  .descr = "Show the daemon's installation directory on the selected"
	  " nodes." },
	{ .tag = "list versions",
	  .descr = "Show the daemon's version and the revision of the"
	  " corresponding RPM on the selected nodes." },
	{ .tag = "list jobs [state {r|p|s}] [slots] [<tid>]",
	  .descr = "Show the jobs within the system. Using the 'state' option,"
	  " only specific classes of jobs will be displayed. The 'slots'"
	  " option enables to show the job's allocated processing slots. If"
	  " <tid> is given, only information concerning the job the process"
	  " <tid> is connected to is displayed." },
	{ NULL, NULL }
    },
    .comment = NULL
};

static info_t resolveInfo = {
    .head = "Resolve command:",
    .syntax = (syntax_t[]) {{
	.cmd = "resolve",
	.arg = "<nodes>"
    }},
    .nodes = 1,
    .descr = "Resolve mapping of ParaStation ID to hostname on all selected"
    " nodes.",
    .tags = NULL,
    .comment = NULL
};

static info_t echoInfo = {
    .head = "Echo command:",
    .syntax = (syntax_t[]) {{
	.cmd = "echo",
	.arg = "<line>"
    }},
    .nodes = 0,
    .descr = "Echo the given <line> to stdout. This command does not support"
    " control sequences like its counterpart /bin/echo.",
    .tags = NULL,
    .comment = NULL
};

static info_t sleepInfo = {
    .head = "Sleep command:",
    .syntax = (syntax_t[]) {{
	.cmd = "sleep",
	.arg = "<sec>"
    }},
    .nodes = 0,
    .descr = "Sleep for <sec> seconds before continuing to parse input.",
    .tags = NULL,
    .comment = NULL
};

static info_t versionInfo = {
    .head = "Version command:",
    .syntax = (syntax_t[]) {{
	.cmd = "version",
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
	.cmd = "{exit | quit}",
	.arg = ""
    }},
    .nodes = 0,
    .descr = "Exit the ParaStation administration tool.",
    .tags = NULL,
    .comment = NULL
};

static info_t pluginInfo = {
    .head = "Plugin command (privileged):",
    .syntax = (syntax_t[]) {{
	.cmd = "plugin",
	.arg = "{ avail | list | {add|load | delete|remove|rm|unload"
	" | forceunload|forceremove | help } <plugin>"
	" | show <plugin> [key <key>] | set <plugin> <key> <value>"
	" | unset <plugin> <key> | loadtime [plugin <plugin>] } <nodes>"
    }},
    .nodes = 1,
    .descr = "Handle plugins on the selected nodes.",
    .tags = (taggedInfo_t[]) {
	{ .tag = "plugin avail",
	  .descr = "Show the available plugins on the selected nodes."
	  " The info displayed includes all plugins available in the daemon's "
	  " search path for plugins." },
	{ .tag = "plugin list",
	  .descr = "Show the currently loaded plugins on the selected nodes."
	  " The info displayed includes name and version of the plugin plus a"
	  " list of plugins requiring the plugin to be loaded." },
	{ .tag = "plugin {add|load} <plugin>",
	  .descr = "Load plugin named <plugin> on the selected nodes. This"
	  " might trigger more plugins required by <plugin> to be loaded." },
	{ .tag = "plugin {delete|remove|rm|unload} <plugin>",
	  .descr = "Unload plugin named <plugin> on the selected nodes. The"
	  " plugin might still be loaded afterward, if it is used by another"
	  " depending plugin or if it ignores the command to unload itself." },
	{ .tag = "plugin {forceremove|forceunload} <plugin>",
	  .descr = "Forcefully unload plugin named <plugin> on the selected"
	  " nodes. The plugin might still be loaded for some time afterward"
	  " until all depending plugin are unloaded and all timeouts are"
	  " expunged." },
	{ .tag = "plugin help <plugin>",
	  .descr = "Request some help-message from the plugin <plugin>." },
	{ .tag = "plugin show <plugin> [key <key>]",
	  .descr = "Show key-value pair indexed by <key> for the given plugin"
	  " <plugin>. If no key is given explicitly, all pairs are"
	  " displayed." },
	{ .tag = "plugin set <plugin> <key> <value>",
	  .descr = "Set key-value pair indexed by <key> to <value> for the"
	  " given plugin <plugin>." },
	{ .tag = "plugin unset <plugin> <key>",
	  .descr = "Unset key-value pair indexed by <key> for the given plugin"
	  " <plugin>." },
	{ .tag = "plugin loadtime [plugin <plugin>]",
	  .descr = "Display the load-time of the plugin <plugin>. If no plugin"
	  " is given explicitly, the load-time of all plugins is displayed." },
	{ NULL, NULL}
    },
    .comment = NULL
};

static info_t envInfo = {
    .head = "Environment command (privileged):",
    .syntax = (syntax_t[]) {{
	.cmd = "environment",
	.arg = "{ list [key <key>] | set <key> <value> | { unset | delete}"
	" <key> } <nodes>"
    }},
    .nodes = 1,
    .descr = "Handle environment on the selected nodes.",
    .tags = (taggedInfo_t[]) {
	{ .tag = "environment list [key <key>]",
	  .descr = "Show the environment on the selected nodes. If 'key <key>'"
	  " is given, only the environment variable <key> and its value is"
	  " displayed." },
	{ .tag = "environment set <key> <value>",
	  .descr = "Set the environment variable <key> to the value <value> on"
	  " the selected nodes." },
	{ .tag = "environment {delete|unset} <key>",
	  .descr = "Unset the environment variable <key> on the selected"
	  " nodes." },
	{ NULL, NULL}
    },
    .comment = NULL
};

static info_t paramInfo = {
    .head = "Parameter command:",
    .syntax = (syntax_t[]) {{
	.cmd = "parameter",
	.arg = "{ show [<key>] | set <key> <value> | help [<key>] }"
    }},
    .nodes = 0,
    .descr = "Handle parameters of the local psiadmin.",
    .tags = (taggedInfo_t[]) {
	{ .tag = "parameter show [<key>]",
	  .descr = "Show the parameters of the local psiadmin. If 'key <key>'"
	  " is given, only the parameter <key> and its value is displayed." },
	{ .tag = "parameter set <key> <value>",
	  .descr = "Set the local psiadmin's parameter <key> to the value"
	  " <value>." },
	{ .tag = "parameter help [<key>]",
	  .descr = "Show some help concerning the parameter <key>. If <key> is"
	  "not given, help on all keys defined is given." },
	{ NULL, NULL}
    },
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
	  .descr = "just an ok is signaled on success." },
	{ .tag = "normal",
	  .descr = "only the coordinator node is telling about its activity."
	},
	{ .tag = "verbose",
	  .descr = "each node is telling about its activity." },
	{ NULL, NULL }
    },
    .comment = NULL
};

/* ---------------------------------------------------------------------- */

/** A long string of separating characters */
static const char sep[] =   "================================================"
"============================================================================";

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
 * Suitable positions for a line wrap are whitespace (' ') characters
 * which are not preceded by pipe ('|') characters, or the
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
    int lwidth = PSC_getWidth() - strlen(tag);

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
	    printf("%*s", PSC_getWidth()-lwidth, "");
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
 * Print the description @a descr preceded by the tag @a tag.
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
 * Suitable positions for a line wrap are whitespace (' ') characters.
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
    int lwidth = PSC_getWidth() - strlen(tag);

    if (descr) {
	char *pos = descr;
	int len = strlen(pos);

	printf("%s", tag);
	while (len>lwidth) {
	    char *end = pos + lwidth - 1;
	    while (*end != ' ') end--;
	    printf("%.*s\n", (int)(end-pos), pos);
	    printf("%*s", PSC_getWidth()-lwidth, "");
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
	    if (len < PSC_getWidth()) printf("%.*s\n", len, sep);
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
