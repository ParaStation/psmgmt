/*
 *               ParaStation3
 * psiadmin_help.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psiadmin_help.c,v 1.2 2002/01/07 08:18:00 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psiadmin_help.c,v 1.2 2002/01/07 08:18:00 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

static void PrintHelp(void)
{
    printf("\n");
    printf("ParaStation Admin: available commands:\n");
    printf("======================================\n");
    printf("\n");
    printf("ADD:      Start ParaStation daemon process on one or all nodes\n");
    printf("KILL:     Terminate a ParaStation process on any node\n");
    printf("CONF:     Print current setting of internal parameters\n");
    printf("STATUS:   Status information\n");
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

static void ParameterInfo(void)
{
    printf("\n");
    printf("NODENAME:   Symbolic name of a machine in the ParaStation"
	   " cluster\n");
    printf("NODENUMBER: Number of a machine in the ParaStation cluster\n");
    printf("            (0 <= number < %d)\n",PSI_getnrofnodes());
    printf("\n");
}

static void PrintAddHelp(void)
{
    printf("\n");
    printf("Add command:\n");
    printf("============\n");
    printf("\n");
    printf("SYNTAX:    ADD\n");
    printf("PARAMETER: NODENUMBER or NODENAME\n");
    ParameterInfo();
    printf("Description: Add starts the ParaStation daemon process (psid) on"
	   " the given node\n");
    printf("             Normally this is done automatically when the system"
	   " comes up.\n");
    printf("\n");
    return;
}

static void PrintSetHelp(void)
{
    printf("\n");
    printf("Set command (privileged):\n");
    printf("=========================\n");
    printf("\n");
    printf("SYNTAX:    SET\n");
    printf("PARAMETER: [NO]USER or [NO]MAXPROC or\n");
    printf("            SMALLPACKETSIZE or RESENDTIMEOUT or DEBUGMASK\n");
    printf("\n");
    printf("Description: SET USER USERNAME      grants access to a particular"
	   " user\n");
    printf("             SET NOUSER             grants access to any users\n");
    printf("             SET MAXPROC NUMBER     set maximum ParaStation"
	   " processes per node\n");
    printf("             SET NOMAXPROC          allow any number of"
	   " ParaStation processes per node\n");
    printf("             SET RDPDEBUG VALUE [NODE] set verbose level for RDP"
	   " protocol\n");
    printf("             SET PSIDDEBUG NODE     set verbose mode of psid"
	   " on\n");
    printf("             SET NOPSIDDEBUG NODE   set verbose mode of psid"
	   " off\n");
    printf("             SET SMALLPACKETSIZE SIZE  set the max size of PIO"
	   " packets\n");
    printf("             SET RESENDTIMEOUT TIME  set retansmission timeout"
	   " (in us)\n");
    printf("             SET DEBUGMASK NUMBER   set the local debugmask\n");
    printf("\n");
    return;
}

static void PrintShowHelp(void)
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

static void PrintStatHelp(void)
{
    printf("\n");
    printf("Status command:\n");
    printf("===============\n");
    printf("\n");
    printf("SYNTAX:    STATUS\n");
    printf("PARAMETER: [NODE] or [COUNT RDP PROC ALL]\n");
    ParameterInfo();
    printf("Description: STATUS NODE   shows the active node(s) of a"
	   " ParaStation cluster.\n");
    printf("             STATUS COUNT  shows the counters of active node(s) in"
	   " a\n");
    printf("                         ParaStation cluster.\n");
    printf("             STATUS RDP    shows the status of the RDP protocol of"
	   " active node(s)\n");
    printf("                         in a ParaStation cluster.\n");
    printf("             STATUS PROC   shows processes using ParaStation on"
	   " active node(s)\n");
    printf("                         in a ParaStation cluster.\n");
    printf("             STATUS LOAD   shows load using ParaStation on active"
	   " node(s)\n");
    printf("                         in a ParaStation cluster.\n");
    printf("             STATUS ALL    shows all statistics given above of all"
	   " nodes in a\n");
    printf("                         ParaStation cluster\n");
    printf("\n");
    printf("For more information type HELP STATUS <subcommand>\n");
    printf("\n");
    return;
}

static void PrintStatNodeHelp(void)
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

static void PrintPsidDebugHelp(void)
{
    printf("\n");
    printf("Setting Debugging mode of the daemon:\n");
    printf("====================================\n");
    printf("\n");
    printf("SYNTAX:    SET PSIDDEBUG NODENUMER\n");
    printf("SYNTAX:    SET NOPSIDDEBUG NODENUMER\n");
    printf("\n");
    printf("       sets debugging verbose mode of the daemon on node"
	   " NODENUMBER\n");
    printf("       on or off.\n");
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
    printf("           FROM: a integer number for the first node which should"
	   " be reseted");
    printf("           TO  : a integer number for the last node which should"
	   " be reseted");
    printf("\n");
    printf("Description: Resetting the network enforces all ParaStation"); 
    printf("             (or subrange [FROM,TO]) daemon processes\n");
    printf("             to put the interface boards in a known state. During"
	   " a memory\n"); 
    printf("             reset the message-pool one each node is reorganized."
	   " As a \n");
    printf("             consequence ALL processes using the ParaStation"
	   " cluster\n");
    printf("             are terminated (killed)!\n");
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
    printf("Description: All ParaStation daemon processes are forced to"
	   " reinitialize\n");
    printf("             the ParaStation cluster. As a consequence ALL"
	   " processes using\n");
    printf("             the ParaStation cluster are terminated (killed)!\n");
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
    printf("Description: All ParaStation daemon processes within the"
	   " ParaStation cluster\n");
    printf("             are forced to terminate. As a consequence ALL"
	   " processes using\n");
    printf("             the ParaStation cluster are terminated (killed)!\n");
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
    printf("Description: Kills a process with the given task-id. The task-id"
	   " can be\n");
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
    printf("Description: All communications links in a ParaStation network are"
	   " tested.\n");
    printf("             VERBOSE: each node is telling about his activity\n");
    printf("             NORMAL:  only the coordinator node is telling about"
	   " his activity\n");
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
