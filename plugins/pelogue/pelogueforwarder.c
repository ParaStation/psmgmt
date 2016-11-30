/*
 * ParaStation
 *
 * Copyright (C) 2013 - 2014 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "peloguecomm.h"
#include "peloguelog.h"
#include "peloguescript.h"
#include "pelogue.h"

#include "pluginmalloc.h"

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
static void startPElogue(int prologue, PElogue_Data_t *data, char *filename,
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
static int runPElogueScript(PElogue_Data_t *data, char *scriptname, int root)
{
    int fstat;
    char filename[300];

    snprintf(filename, sizeof(filename), "%s/%s", data->dirScripts, scriptname);

    if ((fstat = checkPELogueFileStats(filename, root)) == 1) {

	/* TODO DEBUG MSG
	mdbg(-1, "%s: executing '%s'\n", __func__, filename);
	*/

	/* redirect stdout and stderr */
	dup2(open("/dev/null", 0), STDOUT_FILENO);
	dup2(open("/dev/null", 0), STDERR_FILENO);

	startPElogue(data->prologue, data, filename, root);
	exit(0);

	/*
	if (status != 0) {
	    fprintf(stdout, "'%s' as '%s' failed, exit:%d\n", filename,
		    root ? "root" : "user", status);
	    return status;
	}
	*/
    } else if (fstat == -1) {
	/* if the prologue file not exists, all is okay */
	exit(0);
    } else {
	fprintf(stdout, "permisson error for '%s' as '%s'\n", filename,
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
 * @param data A pointer to the PElogue_Data structure.
 *
 * @param buf The buffer which will receive the requested absolute filename.
 *
 * @param bufSize The size of the buffer.
 *
 * @return No return value.
 */
static void setPElogueName(char *script, PElogue_Data_t *data, char *buf,
    size_t bufSize)
{
    struct stat sb;
    char path[300], par[50] = { '\0' };

    if (!data->frontend) {
	snprintf(par, sizeof(par), ".parallel");
    }

    if (!strlen(data->scriptname)) {
	snprintf(buf, bufSize, "%s%s", script, par);
	return;
    }

    /* use extended script name */
    snprintf(buf, bufSize, "%s%s.%s", script, par, data->scriptname);
    snprintf(path, sizeof(path), "%s/%s", data->dirScripts, buf);

    if ((stat(path, &sb)) != -1) {
	return;
    }

    /* if the choosen script does not exist, fallback to standard script */
    snprintf(buf, bufSize, "%s%s", script, par);
}

void execPElogueScript(void *datap, int rerun)
{
    Forwarder_Data_t *fwdata = datap;
    PElogue_Data_t *data = fwdata->userData;
    uint32_t i;
    char name[50];

    for (i=0; i<data->env.cnt; i++) {
	if ((data->env.vars[i])) putenv(data->env.vars[i]);
    }

    if (data->prologue) {
	/*
	mdbg(PSMOM_LOG_PELOGUE, "Running prologue script(s) [max %li sec]\n",
		data->timeout);
	*/

	if (rerun == 1) {
	    /* prologue (root) */
	    setPElogueName("prologue", data, name, sizeof(name));
	    runPElogueScript(data, name, 1);
	} else {
	    /* prologue.user (user) */
	    setPElogueName("prologue.user", data, name, sizeof(name));
	    runPElogueScript(data, name, 0);
	}

	//mdbg(PSMOM_LOG_PELOGUE, "Running prologue script(s) finished\n");
    } else {
	/*
	mdbg(PSMOM_LOG_PELOGUE, "Running epilogue script(s) [max %li sec]\n",
		data->timeout);
	*/

	if (rerun == 1) {
	    /* epilogue (root) */
	    setPElogueName("epilogue", data, name, sizeof(name));
	    runPElogueScript(data, name, 1);
	} else {
	    /* epilogue.user (user) */
	    setPElogueName("epilogue.user", data, name, sizeof(name));
	    runPElogueScript(data, name, 0);
	}
	//mdbg(PSMOM_LOG_PELOGUE, "Running epilogue script(s) finished\n");
    }
}
