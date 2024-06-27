/*
 * ParaStation
 *
 * Copyright (C) 2017-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <popt.h>

#include "pscommon.h"
#include "pse.h"
#include "psienv.h"
#include "psipartition.h"

#include "cloptions.h"

/** set debugging mode and np * gdb to control child processes */
static int gdb = 0;
/** don't call gdb with --args option */
static int gdb_noargs = 0;

/** run child processes on synthetic CPUs provided by the
 * Valgrind core (memcheck tool) */
static int valgrind = 0;
static int memcheck = 0;
/** profile child processes on synthetic CPUs provided by the
 * Valgrind core (callgrind tool) */
static int callgrind = 0;

/** just print output, don't run anything */
static int dryrun = 0;

/** flag to set verbose mode */
static int verbose = 0;

/** set mpich 1 compatible mode */
static int mpichcom = 0;

/* PMI options */
static int pmienabletcp = 0;
static int pmienablesockp = 0;
static int pmitmout = 0;
static int pmidebug = 0;
static int pmidebug_client = 0;
static int pmidebug_kvs = 0;
static int pmidis = 0;

/* PMIx options */
static int pmix = 0;

/* process options */
static int np = -1;
static int gnp = -1;
static int ppn = 0;
static int gppn = 0;
static int tpp = 0;
static int gtpp = 0;
static int envtpp = 0;
static int usize = 0;
static mode_t u_mask;
static char *wdir = NULL;
static char *gwdir = NULL;
static char *nList = NULL;
static char *hList = NULL;
static char *hFile = NULL;
static char *nodetype = NULL;
static char *gnodetype = NULL;
static char *psetname;

/* environment flags and options */
static int envall;
static bool execEnvall;
static int genvall;
static char *envlist;
static char *genvlist;
static char *envopt, *envval;
static char *genvopt, *genvval;

/** Accumulated environments to get exported */
static env_t env;
static char *accenvlist;
static char *accgenvlist;

static char *path = NULL;

/* compability options from other mpiexec commands */
static int totalview = 0;
static int ecfn = 0;
static int gdba = 0;
static int noprompt = 0;
static int localroot = 0;
static int exitinfo = 0;
static int exitcode = 0;
static int port = 0;
static char *phrase = NULL;
static char *smpdfile = NULL;

/* options for parastation (psid/logger/forwarder) */
static int sourceprintf = 0;
static int overbook = 0;
static int exclusive = 0;
static int wait = 0;
static int loopnodesfirst = 0;
static int dynamic = 0;
static int fullPartition = 0;
static int mergeout = 0;
static int mergedepth = 0;
static int mergetmout = 0;
static int rusage = 0;
static int timestamp = 0;
static int interactive = 0;
static int maxtime = 0;
static char *sort = NULL;
static char *dest = NULL;

/* debug options */
static int loggerdb = 0;
static int forwarderdb = 0;
static int pscomdb = 0;
static int loggerrawmode = 0;
static int psidb = 0;

/* options for the pscom library */
static int sndbuf = 0;
static int rcvbuf = 0;
static int nodelay = 0;
static int schedyield = 0;
static int retry = 0;
static int sigquit = 0;
static int ondemand = 0;
static int no_ondemand = 0;
static int collectives = 0;
static char *plugindir = NULL;
static char *discom = NULL;
static char *network = NULL;

/* help option flags */
static int help = 0, usage = 0;
static int debughelp = 0, debugusage = 0;
static int extendedhelp = 0, extendedusage = 0;
static int comphelp = 0, compusage = 0;

static int none = 0;
static int version = 0;

/** context for parsing command-line options */
static poptContext optCon;

static void errExit(char *msg) __attribute__ ((noreturn));
/**
 * @brief Print error msg and exit.
 *
 * @return No return value.
 */
static void errExit(char *msg)
{
    poptPrintUsage(optCon, stderr, 0);
    fprintf(stderr, "\n%s\n\n", msg ? msg : "Exit for unknown reason");
    exit(EXIT_FAILURE);
}

/**
 * @brief Print version info.
 *
 * @return No return value.
 */
static void printVersion(void)
{
    fprintf(stderr, "mpiexec %s\n", PSC_getVersionStr());
}

/* Set up the popt help tables */
static struct poptOption poptCommonOptions[] = {
    { "np", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
      &np, 0, "number of processes to start", "num"},
    { "gnp", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
      &gnp, 0, "global number of processes to start", "num"},
    { "n", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
      &np, 0, "equal to np: number of processes to start", "num"},
    { "exports", 'e', POPT_ARG_STRING,
      &envlist, 'l', "environment to export to this executable", "envlist"},
    { "envall", 'x', POPT_ARG_NONE,
      &envall, 0, "export all environment to this executable", NULL},
    { "env", 'E', POPT_ARG_STRING,
      &envopt, 'e', "export environment and value to this executable",
      "<name> <value>"},
    { "envval", '\0', POPT_ARG_STRING | POPT_ARGFLAG_DOC_HIDDEN,
      &envval, 'v', "", ""},
    { "genv", '\0', POPT_ARG_STRING,
      &genvopt, 'E', "export environment and value globally", "<name> <value>"},
    { "genvval", '\0', POPT_ARG_STRING | POPT_ARGFLAG_DOC_HIDDEN,
      &genvval, 'V', "", ""},
    { "bnr", 'b', POPT_ARG_NONE,
      &mpichcom, 0, "enable ParaStation4 compatibility mode", NULL},
    { "usize", 'u', POPT_ARG_INT,
      &usize, 0, "set the universe size", "num"},
    { "pmix", '\0', POPT_ARG_NONE,
      &pmix, 0, "enable PMIx support (disables PMI)", NULL},
    { "timeout", '\0', POPT_ARG_INT,
      &maxtime, 0, "maximum number of seconds the job is permitted to run",
      "timeout"},
    { "pset", '\0', POPT_ARG_STRING,
      &psetname, 0, "user-specified name assigned to this executable", "name" },
    POPT_TABLEEND
};

static struct poptOption poptPlacementOptions[] = {
    { "nodes", 'N', POPT_ARG_STRING,
      &nList, 0, "list of nodes to use: nodelist <3-5,7,11-17>", "nodelist"},
    { "hosts", 'H', POPT_ARG_STRING,
      &hList, 0, "list of hosts to use: hostlist <node-01 node-04>", "hostlist"},
    { "hostfile", 'f', POPT_ARG_STRING,
      &hFile, 0, "hostfile to use", "file"},
    { "machinefile", 'f', POPT_ARG_STRING,
      &hFile, 0, "machinefile to use, equal to hostfile", "file"},
    { "wait", 'w', POPT_ARG_NONE,
      &wait, 0, "wait for enough resources", NULL},
    { "overbook", 'o', POPT_ARG_NONE,
      &overbook, 0, "allow overbooking", NULL},
    { "loopnodesfirst", 'F', POPT_ARG_NONE,
      &loopnodesfirst, 0, "place consecutive processes on different nodes, "
      "if possible", NULL},
    { "dynamic", '\0', POPT_ARG_NONE, &dynamic, 0,
      "allow dynamic extension of resources via batch-system", NULL },
    { "fullpartition", 'P', POPT_ARG_NONE,
      &fullPartition, 0, "create partition from full list or file", NULL},
    { "exclusive", 'X', POPT_ARG_NONE,
      &exclusive, 0, "do not allow any other processes on used node(s)", NULL},
    { "sort", 'S', POPT_ARG_STRING,
      &sort, 0, "sorting criterion to use: {proc|load|proc+load|none}", "type"},
    { "maxtime", '\0', POPT_ARG_INT,
      &maxtime, 0, "maximum number of seconds the job is permitted to run",
      "maxtime"},
    { "ppn", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
      &ppn, 0, "maximum number of processes per node (0 is unlimited)", "num"},
    { "gppn", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
      &gppn, 0, "global maximum number of processes per node (0 is unlimited)",
      "num"},
    { "tpp", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
      &tpp, 0, "number of threads per process", "num"},
    { "gtpp", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
      &gtpp, 0, "global number of threads per process", "num"},
    { "nodetype", '\0', POPT_ARG_STRING,
      &nodetype, 0, "comma separated list of local node types", "typelist"},
    { "gnodetype", '\0', POPT_ARG_STRING,
      &gnodetype, 0, "comma separated list of global node types", "typelist"},
    POPT_TABLEEND
};

static struct poptOption poptCommunicationOptions[] = {
    { "discom", 'c', POPT_ARG_STRING,
      &discom, 0, "disable a communication architecture: {SHM,TCP,P4SOCK,"
      "GM,MVAPI,OPENIB,DAPL}", "arch"},
    { "network", 't', POPT_ARG_STRING,
      &network, 0, "set a space separated list of networks enabled", "networks"},
    { "schedyield", 'y', POPT_ARG_NONE,
      &schedyield, 0, "use sched yield system call", NULL},
    { "retry", 'r', POPT_ARG_INT,
      &retry, 0, "number of connection retries", "num"},
    { "collectives", 'C', POPT_ARG_NONE,
      &collectives, 0, "enable psmpi collectives", NULL},
    { "ondemand", 'O', POPT_ARG_NONE,
      &ondemand, 0, "use psmpi \"on demand/dynamic\" connections", NULL},
    { "no_ondemand", '\0', POPT_ARG_NONE,
      &no_ondemand, 0, "disable psmpi \"on demand/dynamic\" connections",
      NULL},
    POPT_TABLEEND
};

static struct poptOption poptIOOptions[] = {
    { "interactive", 'i', POPT_ARG_NONE,
      &interactive, 0, "set interactive mode (similar to ssh -t)", NULL},
    { "inputdest", 's', POPT_ARG_STRING,
      &dest, 0, "direction to forward input: dest <1,2,5-10> or <all>", "dest"},
    { "sourceprintf", 'l', POPT_ARG_NONE,
      &sourceprintf, 0, "print output-source info", NULL},
    { "rusage", 'R', POPT_ARG_NONE,
      &rusage, 0, "print consumed sys/user time", NULL},
    { "merge", 'm', POPT_ARG_NONE,
      &mergeout, 0, "merge similar output from different ranks", NULL},
    { "timestamp", 'T', POPT_ARG_NONE,
      &timestamp, 0, "print detailed time-marks", NULL},
    { "wdir", 'd', POPT_ARG_STRING,
      &wdir, 0, "working directory for remote process", "directory"},
    { "umask", '\0', POPT_ARG_INT,
      &u_mask, 0, "umask for remote process", NULL},
    { "path", 'p', POPT_ARG_STRING,
      &path, 0, "the path to search for executables", "directory"},
    POPT_TABLEEND
};

static struct poptOption poptDebugOptions[] = {
    { "loggerdb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &loggerdb, 0, "set debug mode of the logger", NULL},
    { "forwarderdb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &forwarderdb, 0, "set debug mode of the forwarder", NULL},
    { "pscomdb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &pscomdb, 0, "set debug mode of the pscom lib", NULL},
    { "psidb", '\0', POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN,
      &psidb, 0, "set debug mode of the pse/psi lib", "mask"},
    { "pmidb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &pmidebug, 0, "set debug mode of PMI", NULL},
    { "pmidbclient", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &pmidebug_client, 0, "set debug mode of PMI client only", NULL},
    { "pmidbkvs", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &pmidebug_kvs, 0, "set debug mode of PMI KVS only", NULL},
    { "loggerrawmode", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &loggerrawmode, 0, "set raw mode of the logger", NULL},
    { "sigquit", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &sigquit, 0, "pscom: output debug information on signal SIGQUIT", NULL},
    { "show", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &dryrun, 0, "show command for remote execution but don`t run it", NULL},
    POPT_TABLEEND
};

static struct poptOption poptAdvancedOptions[] = {
    { "plugindir", '\0', POPT_ARG_STRING | POPT_ARGFLAG_DOC_HIDDEN,
      &plugindir, 0, "set the directory to search for plugins", "directory"},
    { "sndbuf", '\0', POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN,
      &sndbuf, 0, "set the TCP send buffer size", "<default 32k>"},
    { "rcvbuf", '\0', POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN,
      &rcvbuf, 0, "set the TCP receive buffer size", "<default 32k>"},
    { "delay", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &nodelay, 0, "don't use the NODELAY option for TCP sockets", NULL},
    { "mergedepth", '\0', POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN,
      &mergedepth, 0, "set over how many lines should be searched", "depth"},
    { "mergetimeout", '\0', POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN,
      &mergetmout, 0, "set the time how long an output is maximal delayed",
      "maxdelay"},
    { "pmitimeout", '\0', POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN,
      &pmitmout, 0, "set a timeout till all clients have to join the first "
      "barrier (disabled=-1) (default=60sec + np*0,1sec)", "timeout"},
    { "pmiovertcp", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &pmienabletcp, 0, "connect to the PMI client over TCP/IP", NULL},
    { "pmioverunix", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &pmienablesockp, 0, "connect to the PMI client over unix domain "
      "socket (default)", NULL},
    { "pmidisable", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &pmidis, 0, "disable PMI interface", NULL},
    POPT_TABLEEND
};

static struct poptOption poptCompatibilityOptions[] = {
    { "bnr", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &mpichcom, 0, "Enable ParaStation4 compatibility mode", NULL},
    { "machinefile", 'f',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &hFile, 0, "machinefile to use, equal to hostfile", "file"},
    { "1", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &none, 0, "override default of trying first (ignored)", NULL},
    { "ifhn", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &network, 0, "set a space separated list of networks enabled", NULL},
    { "file", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &none, 0, "file with additional information (ignored)", NULL},
    { "tv", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &totalview, 0, "run processes under totalview (ignored)", NULL},
    { "tvsu", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &totalview, 0, "totalview startup only (ignored)", NULL},
    { "gdb", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &gdb, 0, "debug processes with gdb", NULL},
    { "valgrind", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &valgrind, 0, "debug processes with Valgrind", NULL},
    { "memcheck", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &memcheck, 0, "debug processes with Valgrind (leak-check=full)", NULL},
    { "callgrind", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &callgrind, 0,
      "profile processes with Callgrind (a Valgrind tool)", NULL},
    { "gdba", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &gdba, 0, "attach to debug processes with gdb (ignored)", NULL},
    { "ecfn", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &ecfn, 0, "output xml exit codes filename (ignored)", NULL},
    { "wdir", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &wdir, 0, "working directory for remote process(es)", "directory"},
    { "dir", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &wdir, 0, "working directory for remote process(es)", "directory"},
    { "umask", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &u_mask, 0, "umask for remote process", NULL},
    { "path", 'p',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &path, 0, "place to look for executables", "directory"},
    { "host", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &hList, 0, "host to start on", NULL},
    { "soft", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &none, 0, "giving hints instead of a precise number for the number"
      " of processes (ignored)", NULL},
    { "arch", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &nodetype, 0, "equal to nodetype", NULL},
    { "envall", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &envall, 0, "export all environment to this executable", NULL},
    { "envnone", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &none, 0, "export no environment (ignored)", NULL},
    { "envlist", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &envlist, 'l', "export list of environment to this executable",
      "envlist"},
    { "env", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &envopt, 'e', "set environment to value for this executable",
      "<name> <value>"},
    { "usize", 'u',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &usize, 0, "set the universe size", NULL},
    { "maxtime", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &maxtime, 0, "maximum number of seconds the job is permitted to run",
      "INT"},
    { "timeout", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &maxtime, 0, "maximum number of seconds the job is permitted to run",
      "INT"},
    { "noprompt", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &noprompt, 0, "not supported, will be ignored", NULL},
    { "localroot", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &localroot, 0, "not supported, will be ignored", NULL},
    { "exitinfo", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &exitinfo, 0, "not supported, will be ignored", NULL},
    { "exitcode", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &exitcode, 0, "not supported, will be ignored", NULL},
    { "port", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &port, 0, "not supported, will be ignored", NULL},
    { "phrase", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &phrase, 0, "not supported, will be ignored", NULL},
    { "smpdfile", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &smpdfile, 0, "not supported, will be ignored", NULL},
     POPT_TABLEEND
};

static struct poptOption poptCompatibilityGlobalOptions[] = {
    { "gn", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &gnp, 0, "equal to gnp: global number of processes to start", "num"},
    { "gwdir", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &gwdir, 0, "working directory for remote process(es)", "directory"},
    { "gdir", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &gwdir, 0, "working directory for remote process(es)", "directory"},
    { "gumask", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &u_mask, 0, "umask for remote process", NULL},
    { "gpath", 'p',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &path, 0, "place to look for executables", "directory"},
    { "ghost", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &hList, 0, "host to start on", NULL},
    { "gsoft", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &none, 0, "giving hints instead of a precise number for the number"
      " of processes (ignored)", NULL},
    { "garch", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &gnodetype, 0, "equal to gnodetype", NULL},
    { "genvall", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &genvall, 0, "export all environment globally", NULL},
    { "genvnone", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &none, 0, "export no environment globally (ignored)", NULL},
    { "genvlist", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &genvlist, 'L', "export list of environment globally", "envlist"},
    { "genv", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &genvopt, 'E', "set environment to value globally", "<name> <value>"},
    POPT_TABLEEND
};

static struct poptOption poptOtherOptions[] = {
    { "gdb", '\0', POPT_ARG_NONE,
      &gdb, 0, "debug processes with gdb", NULL},
    { "valgrind", '\0', POPT_ARG_NONE,
      &valgrind, 0, "debug processes with Valgrind", NULL},
    { "memcheck", '\0', POPT_ARG_NONE,
      &memcheck, 0, "debug processes with Valgrind (leak-check=full)", NULL},
    { "callgrind", '\0', POPT_ARG_NONE, &callgrind, 0,
      "profile processes with Callgrind (a Valgrind tool)", NULL},
    { "noargs", '\0', POPT_ARG_NONE,
      &gdb_noargs, 0, "don't call gdb with --args", NULL},
    { "verbose", 'v', POPT_ARG_NONE,
      &verbose, 0, "set verbose mode", NULL},
    { "version", 'V', POPT_ARG_NONE,
      &version, -1, "output version information and exit", NULL},
    POPT_TABLEEND
};

static struct poptOption poptMoreHelpOptions[] = {
    { "extendedhelp", '\0', POPT_ARG_NONE,
      &extendedhelp, 0, "display extended help", NULL},
    { "extendedusage", '\0', POPT_ARG_NONE,
      &extendedusage, 0, "print extended usage", NULL},
    { "debughelp", '\0', POPT_ARG_NONE,
      &debughelp, 0, "display debug help", NULL},
    { "debugusage", '\0', POPT_ARG_NONE,
      &debugusage, 0, "print debug usage", NULL},
    { "comphelp", '\0', POPT_ARG_NONE,
      &comphelp, 0, "display compatibility help", NULL},
    { "compusage", '\0', POPT_ARG_NONE,
      &compusage, 0, "print compatibility usage", NULL},
    { "help", 'h', POPT_ARG_NONE,
      &help, 0, "show help message", NULL},
    { "usage", '?', POPT_ARG_NONE,
      &usage, 0, "print usage", NULL},
    POPT_TABLEEND
};

static struct poptOption optionsTable[] = {
    { NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptCommonOptions, \
      0, "Common options:", NULL },
    { NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptPlacementOptions, \
      0, "Job placement options:", NULL },
    { NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptCommunicationOptions, \
      0, "Communication options:", NULL },
    { NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptIOOptions, \
      0, "I/O options:", NULL },
    { NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptDebugOptions, \
      0, NULL , NULL },
    { NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptAdvancedOptions, \
      0, NULL , NULL },
    { NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptCompatibilityOptions, \
      0, NULL , NULL },
    { NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptCompatibilityGlobalOptions, \
      0, NULL , NULL },
    { NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptOtherOptions, \
      0, "Other options:", NULL },
    { NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptMoreHelpOptions, \
      0, "Help options:", NULL },
    POPT_TABLEEND
};

/**
 * @brief Output the usage which is normally hidden from the user like
 * debug options.
 *
 * @param opt Table which holds all hidden options.
 *
 * @param argc Number of arguments in @a argv
 *
 * @param argv Argument vector
 *
 * @return No return value
 */
static void printHiddenUsage(poptOption opt, int argc, const char *argv[],
			     char *headline)
{
    for (poptOption o = opt; o->longName || o->shortName || o->arg; o++) {
	o->argInfo = o->argInfo ^ POPT_ARGFLAG_DOC_HIDDEN;
    }

    poptFreeContext(optCon);
    optCon = poptGetContext(NULL, argc, argv, opt, 0);
    fprintf(stdout, "\n%s\n", headline);
    poptPrintUsage(optCon, stdout, 0);
}

/**
 * @brief Output the help which is normally hidden from the user like
 * debug options.
 *
 * @param opt Table which holds all hidden options.
 *
 * @param argc Number of arguments in @a argv
 *
 * @param argv Argument vector
 *
 * @return No return value
 */
static void printHiddenHelp(poptOption opt, int argc, const char *argv[],
			    char *headline)
{
    poptOption o = opt;
    while(o->longName || o->shortName || o->arg) {
	o->argInfo = o->argInfo ^ POPT_ARGFLAG_DOC_HIDDEN;
	o++;
    }

    poptFreeContext(optCon);
    optCon = poptGetContext(NULL, argc, argv, opt, 0);
    fprintf(stdout, "\n%s\n", headline);
    poptPrintHelp(optCon, stdout, 0);
}

/**
 * Print help and usage information
 *
 * @param argc Number of arguments in @a argv
 *
 * @param argv Argument vector
 *
 * @return No return value
 */
static void printHelp(int argc, const char *argv[])
{
    /* output help */
    if (help) {
	poptPrintHelp(optCon, stdout, 0);
	exit(EXIT_SUCCESS);
    }

    /* output usage */
    if (usage) {
	poptPrintUsage(optCon, stdout, 0);
	exit(EXIT_SUCCESS);
    }

    /* output extended help */
    if (extendedhelp) {
	printHiddenHelp(poptAdvancedOptions, argc, argv, "Advanced Options:");
	exit(EXIT_SUCCESS);
    }

    /* output extended usage */
    if (extendedusage) {
	printHiddenUsage(poptAdvancedOptions, argc, argv, "Advanced Options:");
	exit(EXIT_SUCCESS);
    }

    /* output debug help */
    if (debughelp) {
	printHiddenHelp(poptDebugOptions, argc, argv, "Debug Options:");
	exit(EXIT_SUCCESS);
    }

    /* output debug usage */
    if (debugusage) {
	printHiddenUsage(poptDebugOptions, argc, argv, "Debug Options:");
	exit(EXIT_SUCCESS);
    }

    /* output compatibility usage */
    if (compusage) {
	printHiddenUsage(poptCompatibilityOptions, argc, argv,
			 "Compatibility Options:");
	printHiddenUsage(poptCompatibilityGlobalOptions, argc, argv,
			 "Global Compatibility Options:");
	exit(EXIT_SUCCESS);
    }

    /* output compatibility help */
    if (comphelp) {
	printHiddenHelp(poptCompatibilityOptions, argc, argv,
			"Compatibility Options:");
	printHiddenHelp(poptCompatibilityGlobalOptions, argc, argv,
			"Global Compatibility Options:");
	exit(EXIT_SUCCESS);
    }

    /* print Version */
    if (version) {
	printVersion();
	exit(EXIT_SUCCESS);
    }
}

/**
 * @brief Determine hardware type
 *
 * Resolve the hardware type provided in the string @a hwStr and
 * encode it in a bit-array to be returned. @a hwStr contains a list
 * of hardware names as described in the ParaStation
 * configuration. The list is expected to be space, comma or newline
 * separated.
 *
 * @param hwStr Character string describing the requested HW type
 *
 * @return Bit-array encoding the hardware types
 */
static uint32_t getNodeType(char *hwStr)
{
    static char **hwList = NULL;
    static unsigned int hwListSize = 0;
    const char delimiters[] =", \n";
    char msgStr[512];
    char *next, *toksave;
    unsigned int count = 0;
    uint32_t hwType = 0;

    if (verbose) printf("setting node type to '%s'\n", hwStr);

    next = strtok_r(hwStr, delimiters, &toksave);
    while (next) {
	if (count+1 >= hwListSize) {
	    hwListSize += 8;
	    hwList = realloc(hwList, hwListSize * sizeof(*hwList));
	    if (!hwList) {
		snprintf(msgStr, sizeof(msgStr), "%s: realloc(): %m", __func__);
		errExit(msgStr);
	    }
	}
	hwList[count++] = next;
	next = strtok_r(NULL, delimiters, &toksave);
    }
    if (!hwList) {
	snprintf(msgStr, sizeof(msgStr), "%s: empty hwStr '%s'?", __func__,
		 hwStr);
	errExit(msgStr);
    }

    hwList[count] = NULL;

    if (PSI_resolveHWList(hwList, &hwType) == -1) {
	snprintf(msgStr, sizeof(msgStr),
		 "%s: getting hardware type '%s' failed.", __func__, hwStr);
	errExit(msgStr);
    }

    return hwType;
}

/**
 * @brief Save executable specific options
 *
 * Save exexcutable specific option to the command-line option
 * configuration @a conf. There are @a argc executable specific option
 * expected in the argument vector @a argv. Further options might be
 * taken from the global variables @ref np, @ref gnp, @ref nodetype,
 * @ref gnodetype, @ref ppn, @ref gppn, @ref ttp, @ref gttp, @ref
 * envttp, @ref wdir, and @ref gwdir.
 *
 * @param conf Command-line options configration used to store the
 * executable information in
 *
 * @param argc Number of arguments in @a argv
 *
 * @param argv Argument vector describing the executable to store
 *
 * @return No return value
 */
static void saveNextExecutable(Conf_t *conf, int argc, const char **argv)
{
    char *hwTypeStr, msgStr[128];

    if (conf->execCount >= conf->execMax) {
	conf->execMax += 16;
	Executable_t *newExec = realloc(conf->exec,
					conf->execMax * sizeof(*conf->exec));
	if (!newExec) {
	    fprintf(stderr, "%s: realloc(): %m\n", __func__);
	    exit(EXIT_FAILURE);
	}
	conf->exec = newExec;
    }
    Executable_t *exec = &conf->exec[conf->execCount];

    if (argc <= 0 || !argv || !argv[0]) errExit("invalid colon syntax\n");

    if (np > 0) {
	exec->np = np;
	np = -1;
    } else if (gnp > 0) {
	exec->np = gnp;
    } else {
	fprintf(stderr, "no -np argument for binary %i: '%s'\n",
		conf->execCount + 1, argv[0]);
	exit(EXIT_FAILURE);
    }
    conf->np += exec->np;

    if (nodetype) {
	hwTypeStr = nodetype;
	nodetype = NULL;
    } else if (gnodetype) {
	hwTypeStr = gnodetype;
    } else {
	hwTypeStr = NULL;
    }
    exec->hwType = hwTypeStr ? getNodeType(hwTypeStr) : 0;

    if (ppn) {
	exec->ppn = ppn;
	ppn = 0;
    } else if (gppn) {
	exec->ppn = gppn;
    } else {
	exec->ppn = 0;
    }

    if (tpp) {
	exec->tpp = tpp;
	tpp = 0;
    } else if (gtpp) {
	exec->tpp = gtpp;
    } else if (envtpp) {
	exec->tpp = envtpp;
    } else {
	exec->tpp = 1;
    }
    if (exec->tpp > conf->maxTPP) conf->maxTPP = exec->tpp;
    if (wdir) {
	exec->wdir = wdir;
	wdir = NULL;
    } else if (gwdir) {
	exec->wdir = gwdir;
    } else {
	exec->wdir = NULL;
    }
    exec->argc = argc;
    exec->argv = malloc((argc + 1) * sizeof(*exec->argv));
    if (!exec->argv) {
	snprintf(msgStr, sizeof(msgStr), "%s: malloc(): %m", __func__);
	errExit(msgStr);
    }
    for (int i = 0; i < argc; i++) {
	exec->argv[i] = strdup(argv[i]);
	if (!exec->argv[i]) {
	    snprintf(msgStr, sizeof(msgStr), "%s: strdup(%s): %m", __func__,
		     argv[i]);
	    errExit(msgStr);
	}
    }
    exec->argv[argc] = NULL;

    exec->env = env;
    env = envNew(NULL);
    exec->envList = accenvlist;
    accenvlist = NULL;
    exec->envall = envall;
    if (envall) execEnvall = true;
    envall = 0;

    exec->psetname = psetname;
    psetname = NULL;

    conf->execCount++;
}

/**
 * @brief Check sanity
 *
 * Perform some sanity checks to handle common mistakes after parsing
 * of command-line arguments is finished.
 *
 * @return No return value
 */
static void checkSanity(void)
{
    if (totalview) {
	errExit("totalview is not yet implemented\n");
    }

    if (gdba) {
	errExit("gdba is not yet implemented\n");
    }

    if (ondemand && no_ondemand) {
	errExit("Options --ondemand and --no_ondemand are mutually exclusive");
    }

    /* display warnings for not supported env variables/options */
    if (getenv("MPIEXEC_PORT_RANGE")) {
	fprintf(stderr, "MPIEXEC_PORT_RANGE is not supported, ignoring it\n");
    }

    if (getenv("MPD_CON_EXT")) {
	fprintf(stderr, "MPD_CON_EXT is not supported, ignoring it\n");
    }

    if (getenv("MPIEXEC_PREFIX_STDOUT")) {
	fprintf(stderr, "MPIEXEC_PREFIX_STDOUT is not supported, use "
		"--sourceprintf instead\n");
    }

    if (getenv("MPIEXEC_PREFIX_STDERR")) {
	fprintf(stderr, "MPIEXEC_PREFIX_STDERR is not supported, use "
		"--sourceprintf instead\n");
    }

    if (getenv("MPIEXEC_STDOUTBUF")) {
	fprintf(stderr, "MPIEXEC_STDOUTBUF is not supported, use "
		"--merge instead\n");
    }

    if (getenv("MPIEXEC_STDERRBUF")) {
	fprintf(stderr, "MPIEXEC_STDERRBUF is not supported, use "
		"--merge instead\n");
    }

    if (ecfn) fprintf(stderr, "ecfn is not yet implemented -> ignored\n");

    if (noprompt) fprintf(stderr, "-noprompt is not supported -> ignored\n");

    if (localroot) fprintf(stderr, "-localroot is not supported -> ignored\n");

    if (exitinfo) fprintf(stderr, "-exitinfo is not supported -> ignored\n");

    if (exitcode) fprintf(stderr, "-exitcode is not supported,"
			  " use --loggerdb instead\n");

    if (port) fprintf(stderr, "-port is not supported -> ignored\n");

    if (phrase) fprintf(stderr, "-phrase is not supported -> ignored\n");

    if (smpdfile) fprintf(stderr, "-smpdfile is not supported -> ignored\n");
}

/**
 * @brief Check configuration's consistency
 *
 * Perform some consistency checks on configuration @a conf to handle
 * common mistakes.
 *
 * @param conf Configuration to check
 *
 * @return No return value
 */
static void checkConsistency(Conf_t *conf)
{
    char *msg, msgStr[512];

    if (conf->np == -1) {
	errExit("Give at least the -np argument");
    }

    if (conf->np < 1) {
	snprintf(msgStr, sizeof(msgStr), "'-np %d' makes no sense", np);
	errExit(msgStr);
    }

    if (!conf->execCount || !conf->exec[0].argc) {
	errExit("No <command> specified");
    }

    if (conf->gdb) {
	conf->merge = true;
	if (!conf->dest) setenv("PSI_INPUTDEST", "all", 1);
	conf->pmiTmout = -1;
    }

    if (conf->callgrind || conf->memcheck) conf->valgrind = true;
    if (conf->gdb && conf->valgrind) {
	errExit("Don't use GDB and Valgrind together");
    }

    if (conf->gdb && conf->mpichComp) {
	errExit("--gdb only working with mpi2, don't use it with --bnr\n");
    }

    if (conf->mpichComp && conf->execCount > 1) {
	errExit("colon syntax is only supported with mpi2\n");
    }

    if (conf->pmiTCP && conf->pmiSock) {
	errExit("Only one PMI connection type allowed (TCP or Unix)");
    }

    msg = PSE_checkAndSetNodeEnv(conf->nList, conf->hList, conf->hFile,
				 NULL, "--", conf->verbose);
    if (msg) errExit(msg);

    msg = PSE_checkAndSetSortEnv(conf->sort, "--", conf->verbose);
    if (msg) errExit(msg);

    if (conf->fullPartition && !(conf->nList || conf->hList || conf->hFile)) {
	errExit("Full partition only with explicit resources");
    }

    /* PMI interface consistency */
    if (conf->pmiDisable || conf->mpichComp) {
	conf->pmiTCP = false;
	conf->pmiSock = false;
    } else if (!conf->pmiTCP && !conf->pmiSock) {
	/* default PMI connection method is unix socket */
	if (getenv("PMI_ENABLE_TCP")) {
	    conf->pmiTCP = true;
	} else {
	    conf->pmiSock = true;
	}
    }
}

/**
 * @brief Fill in configuration information
 *
 * Fill in information from various global variable into the
 * configuration structure @a conf.
 *
 * @param conf Configuration to check
 *
 * @return No return value
 */
static void setupConf(Conf_t *conf)
{
    if (!conf) return;

    conf->verbose = verbose;
    char *envStr = getenv("MPIEXEC_VERBOSE");
    if (envStr) conf->verbose = true;

    /* np already fixed */
    /* set the universe size */
    conf->uSize = conf->cmdLineUSize = usize;
    envStr = getenv("MPIEXEC_UNIVERSE_SIZE");
    if (envStr && conf->uSize < 1) conf->uSize = atoi(envStr);
    if (conf->uSize < conf->np) conf->uSize = conf->np;
    if (conf->verbose) printf("Set universe size to %i\n", conf->uSize);

    /* exec, execCount, and maxTPP already fixed */
    conf->envTPP = envtpp;

    conf->dryrun = dryrun;
    conf->envall = genvall;
    conf->execEnvall = execEnvall;
    conf->u_mask = u_mask;

    /* resource options */
    if (nList) { conf->nList = strdup(nList); nList = NULL; }
    if (hList) { conf->hList = strdup(hList); hList = NULL; }
    if (hFile) { conf->hFile = strdup(hFile); hFile = NULL; }
    if (sort) { conf->sort = strdup(sort); sort = NULL; }

    conf->overbook = overbook;
    conf->exclusive = exclusive;
    conf->wait = wait;
    conf->loopnodesfirst = loopnodesfirst;
    conf->dynamic = dynamic;
    conf->fullPartition = fullPartition;

    /* special modes */
    conf->gdb = gdb;
    conf->gdb_noargs = gdb_noargs;
    conf->valgrind = valgrind;
    conf->memcheck = memcheck;
    conf->callgrind = callgrind;
    conf->mpichComp = mpichcom;
    if (getenv("MPIEXEC_BNR")) conf->mpichComp = true;

    /* PMI options */
    conf->pmiTCP = pmienabletcp;
    conf->pmiSock = pmienablesockp;
    conf->pmiTmout = pmitmout;
    conf->pmiDbg = pmidebug;
    conf->pmiDbgClient = pmidebug_client;
    conf->pmiDbgKVS = pmidebug_kvs;
    conf->pmiDisable = pmidis;

    /* PMIx options */
    conf->PMIx = pmix;
    if (pmix) conf->pmiDisable = true;

    /* options going to pscom library */
    conf->PSComSndbuf = sndbuf;
    conf->PSComRcvbuf = rcvbuf;
    conf->PSComNoDelay = nodelay;
    conf->PSComSchedYield = schedyield;
    conf->PSComRetry = retry;
    conf->PSComSigQUIT = sigquit;
    conf->PSComOnDemand = -1;
    if (ondemand) conf->PSComOnDemand = 1;
    if (no_ondemand) conf->PSComOnDemand = 0;
    conf->PSComColl = collectives;
    if (plugindir) { conf->PSComPlgnDir = strdup(plugindir); plugindir = NULL; }
    if (discom) { conf->PSComDisCom = strdup(discom); discom = NULL; }
    if (network) { conf->PSComNtwrk = strdup(network); network = NULL; }

    /* options going to psmgmt (psid/logger/forwarder) */
    conf->sourceprintf = sourceprintf;
    conf->merge = mergeout;
    conf->mergeDepth = mergedepth;
    conf->mergeTmout = mergetmout;
    conf->rusage = rusage;
    conf->timestamp = timestamp;
    conf->interactive = interactive;
    conf->maxtime = maxtime;
    if (dest) { conf->dest = strdup(dest); dest = NULL; }
    if (accgenvlist) { conf->envList = accgenvlist; accgenvlist = NULL; }
    if (path) { conf->path = strdup(path); path = NULL; }

    /* debug options */
    conf->loggerDbg = loggerdb;
    conf->forwarderDbg = forwarderdb;
    conf->PSComDbg = pscomdb;
    conf->loggerrawmode = loggerrawmode;
    conf->psiDbgMask = psidb;
}

Conf_t * parseCmdOptions(int argc, const char *argv[])
{
    #define OTHER_OPTIONS_STR "[OPTION...] <command> [cmd_options]"
    const char *nextArg, **leftArgv;
    const char **dup_argv;
    int leftArgc, dup_argc, rc = 0;
    Conf_t *conf = calloc(1, sizeof(*conf));

    /** Set TPP from environment -- if any -- before any command-line parsing */
    char *envStr = getenv("PSI_TPP");
    if (!envStr) envStr = getenv("OMP_NUM_THREADS");
    if (envStr) {
	envtpp = strtol(envStr, NULL, 0);
	/* Propagate explicitly since PSI_* is not */
	setPSIEnv("PSI_TPP", envStr);
    }
    conf->maxTPP = 1;
    env = envNew(NULL);

    /* create context for parsing */
    poptDupArgv(argc, argv, &dup_argc, &dup_argv);

    optCon = poptGetContext(NULL, dup_argc, dup_argv,
			    optionsTable, POPT_CONTEXT_POSIXMEHARDER);
    poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);

PARSE_MPIEXEC_OPT:
    /* parse mpiexec options */
    while ((rc = poptGetNextOpt(optCon)) >= 0) {
	const char *av[] = { "--envval", NULL };
	const char *gav[] = { "--genvval", NULL };

	/* handle special env option */
	switch (rc) {
	case 'e':
	    poptStuffArgs(optCon, av);
	    break;
	case 'v':
	    envSet(env, envopt, envval);
	    break;
	case 'E':
	    poptStuffArgs(optCon, gav);
	    break;
	case 'V':
	    setPSIEnv(genvopt, genvval);
	    break;
	case 'l':
	    if (accenvlist) {
		char *tmp = PSC_concat(accenvlist, ",", envlist);
		free(accenvlist);
		accenvlist = tmp;
	    } else {
		accenvlist = strdup(envlist);
	    }
	    break;
	case 'L':
	    if (accgenvlist) {
		char *tmp = PSC_concat(accgenvlist, ",", genvlist);
		free(accgenvlist);
		accgenvlist = tmp;
	    } else {
		accgenvlist = strdup(genvlist);
	    }
	    break;
	}
    }

    leftArgv = poptGetArgs(optCon);
    leftArgc = 0;

    /* parse leftover arguments */
    while (leftArgv && (nextArg = leftArgv[leftArgc])) {
	leftArgc++;

	if (!strcmp(nextArg, ":")) {

	    /* save current executable and arguments */
	    saveNextExecutable(conf, leftArgc-1, leftArgv);

	    /* create new context with leftover args */
	    dup_argc = 0;
	    dup_argv[dup_argc++] = strdup("mpiexec");

	    while ((nextArg = leftArgv[leftArgc])) {
		dup_argv[dup_argc++] = strdup(nextArg);
		leftArgc++;
	    }

	    poptFreeContext(optCon);
	    optCon = poptGetContext(NULL, dup_argc, dup_argv, optionsTable,
				    POPT_CONTEXT_POSIXMEHARDER);
	    poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);

	    /* continue parsing of sub mpiexec options */
	    goto PARSE_MPIEXEC_OPT;
	}
    }

    if (leftArgv) saveNextExecutable(conf, leftArgc, leftArgv);
    envDestroy(env);

    /* restore original context for further usage messages */
    poptFreeContext(optCon);
    optCon = poptGetContext(NULL, argc, argv, optionsTable,
			    POPT_CONTEXT_POSIXMEHARDER);
    poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);

    if (rc < -1) {
	/* an error occurred during option processing */
	char msgStr[512];
	snprintf(msgStr, sizeof(msgStr), "%s: %s",
		 poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		 poptStrerror(rc));
	errExit(msgStr);
    }

    /* print help messages if necessary */
    printHelp(argc, argv);

    checkSanity();
    setupConf(conf);
    checkConsistency(conf);

    poptFreeContext(optCon);

    return conf;
}

void releaseConf(Conf_t *conf)
{
    if (!conf) return;

    free(conf->nList);
    free(conf->hList);
    free(conf->hFile);
    free(conf->sort);

    free(conf->PSComPlgnDir);
    free(conf->PSComDisCom);
    free(conf->PSComNtwrk);

    free(conf->dest);
    free(conf->envList);
    free(conf->path);

    if (conf->exec) {
	for (int e = 0; e < conf->execCount; e++) {
	    envDestroy(conf->exec[e].env);
	    free(conf->exec[e].envList);
	    if (!conf->exec[e].argv) continue;
	    for (int i = 0; i < conf->exec[e].argc; i++) {
		free(conf->exec[e].argv[i]);
	    }
	    free(conf->exec[e].argv);
	}
    }
    free(conf->exec);
    free(conf);
}
