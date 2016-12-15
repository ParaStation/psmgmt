/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "pscommon.h"
#include "pluginmalloc.h"

#include "peloguecomm.h"
#include "peloguelog.h"
#include "peloguescript.h"
#include "pelogue.h"

#include "pelogueforwarder.h"

/**
 * @brief Prepare the environment and execute a PElog script.
 *
 * @param prologue If true we start a prologue script, else start an
 *	epilogue script.
 *
 *  @param data Pointer to the data structure the PElog script belongs to.
 *
 *  @param root If true we start the script as root, else we start a user
 *	PElog script.
 */
static void startPElogue(int prologue, PElogueChild_t *data, char *filename,
			 int root)
{
    char hname[1024];
    char *argv[100];
    int argc = 0;

    /**
     * prologue cmdline args
     *
     * ($1) PBS_JOBID	    : 1262.michi-ng.localdomain
     * ($2) PBS_USER	    : rauh
     * ($3) PBS_GROUP	    : users
     * ($4) PBS_JOBNAME	    : mom_test
     * ($5) PBS_LIMITS	    : neednodes=1:ppn=2,nodes=1:ppn=2,walltime=00:00:10
     * ($6) PBS_QUEUE	    : batch
     * ($7) PBS_ACCOUNT     :
     */

    /**
     * prologue env vars
     *
     * PBS_MSHOST=michi-ng
     * PBS_RESOURCE_NODES=1:ppn=2
     * PATH=/bin:/usr/bin
     * PWD=/var/spool/torque/mom_priv
     * PBS_NODENUM=0
     * SHLVL=1
     * PBS_NODEFILE=/var/spool/torque/aux//1265.michi-ng.localdomain
     * _=/bin/env
    */

    /**
     * epilogue cmdline args
     *
     * ($1)  PBS_JOBID	    : 1264.michi-ng.localdomain
     * ($2)  PBS_USER	    : rauh
     * ($3)  PBS_GROUP	    : users
     * ($4)  PBS_JOBNAME    : mom_test
     * ($5)  PBS_SESSION_ID : 31813
     * ($6)  PBS_LIMITS	    : neednodes=1:ppn=2,nodes=1:ppn=2,walltime=00:00:10
     * ($7)  PBS_RESOURCES : cput=00:00:05,mem=23700kb,vmem=36720kb,walltime=00:00:05
     * ($8)  PBS_QUEUE	    : batch
     * ($9)  PBS_ACCOUNT    :
     * ($10) PBS_EXIT	    : 0
     */

    /**
     * epilogue env vars
     *
     * PBS_MSHOST=michi-ng
     * PBS_RESOURCE_NODES=1:ppn=2
     * PATH=/bin:/usr/bin
     * PWD=/var/spool/torque/mom_priv
     * PBS_NODENUM=0
     * SHLVL=1
     * PBS_NODEFILE=/var/spool/torque/aux//1266.michi-ng.localdomain
     * _=/bin/env
     */

    /* prepare argv */
    argv[argc++] = filename;

    /* arg1: jobid */
    //argv[argc++] = ustrdup(data->jobid);

    /* arg2: user */
    //argv[argc++] = ustrdup(data->user);

    /* arg3: group */
    //argv[argc++] = ustrdup(data->group);

    /* close argv */
    argv[argc++] = NULL;

    /* TODO: switch to user */
    /*
    if (!root) {
	struct passwd *spasswd;

	if (!(spasswd = getpwnam(data->user))) {
	    mlog("%s: getpwnam(%s) failed\n", __func__, data->user);
	    exit(1);
	}
	switchUser(data->user, spasswd, 0);
    } else {
	*/
	setenv("USER", "root", 1);
	setenv("USERNAME", "root", 1);
	setenv("LOGNAME", "root", 1);
	setenv("HOME", rootHome, 1);
    //}

    /* set some sane defaults */
    gethostname(hname, sizeof(hname));
    setenv("HOSTNAME", hname, 1);
    setenv("LANG", "C", 1);

    if (prologue) {
	setenv("PELOGUE", "prologue", 1);
    } else {
	setenv("PELOGUE", "epilogue", 1);
    }

    /* set tmp directory */
    /*
    if (data->tmpDir) {
	setenv("TMPDIR", data->tmpDir, 1);
    }
    */

    /* pshc will fail with malloc check enabled (pgrep) */
    unsetenv("MALLOC_CHECK_");

    /* start prologue script */
    execvp(argv[0], argv);
}

/**
 * @brief Execute a pelogue script and wait for it to finish.
 *
 * @param data A pointer to the PElogue_Data structure.
 *
 * @param filename The filename of the script to spawn.
 *
 * @param root Flag which should be set to 1 if the script will run as root,
 * otherwise to 0.
 *
 * @return Returns 0 on success or on exit code indicating the error.
 */
static int runPElogueScript(PElogueChild_t *child, char *scriptname, int root)
{
    int fstat;
    char fname[PATH_MAX];

    snprintf(fname, sizeof(fname), "%s/%s", child->dirScripts, scriptname);

    if ((fstat = checkPELogueFileStats(fname, root)) == 1) {

	/* TODO DEBUG MSG
	mdbg(-1, "%s: executing '%s'\n", __func__, fname);
	*/

	/* redirect stdout and stderr */
	dup2(open("/dev/null", 0), STDOUT_FILENO);
	dup2(open("/dev/null", 0), STDERR_FILENO);

	startPElogue(child->type == PELOGUE_PROLOGUE, child, fname, root);
	exit(0);

	/*
	if (status != 0) {
	    fprintf(stdout, "'%s' as '%s' failed, exit:%d\n", fname,
		    root ? "root" : "user", status);
	    return status;
	}
	*/
    } else if (fstat == -1) {
	/* if the prologue file not exists, all is okay */
	exit(0);
    } else {
	fprintf(stdout, "permisson error for '%s' as '%s'\n", fname,
		root ? "root" : "user");
	exit(1);
    }
    exit(0);
}

/**
 * @brief Setup the path and filename to a pelogue script.
 *
 * @param script The name of the pelogue script.
 *
 * @param par Flag to add '.parallel' to the name
 *
 * @param buf The buffer which will receive the requested absolute filename.
 *
 * @param bufSize The size of the buffer.
 *
 * @return No return value.
 */
static void setPElogueName(char *script, bool par, char *buf, size_t bufSize)
{
    snprintf(buf, bufSize, "%s%s", script, par ? ".parallel" : "");
}

void execPElogueScript(void *info, int rerun)
{
    Forwarder_Data_t *fwdata = info;
    PElogueChild_t *child = fwdata->userData;
    bool frntnd = child->mainPElogue == PSC_getMyID();
    uint32_t i;
    char name[50];

    for (i=0; i<child->env.cnt; i++) {
	if (child->env.vars[i]) putenv(child->env.vars[i]);
    }

    if (child->type == PELOGUE_PROLOGUE) {
	if (rerun == 1) {
	    /* prologue (root) */
	    setPElogueName("prologue", !frntnd, name, sizeof(name));
	    runPElogueScript(child, name, 1);
	} else {
	    /* prologue.user (user) */
	    setPElogueName("prologue.user", !frntnd, name, sizeof(name));
	    runPElogueScript(child, name, 0);
	}
    } else {
	if (rerun == 1) {
	    /* epilogue (root) */
	    setPElogueName("epilogue", !frntnd, name, sizeof(name));
	    runPElogueScript(child, name, 1);
	} else {
	    /* epilogue.user (user) */
	    setPElogueName("epilogue.user", !frntnd, name, sizeof(name));
	    runPElogueScript(child, name, 0);
	}
    }
}
