/*
 *               ParaStation3
 * psidspawn.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidspawn.c,v 1.2 2002/07/31 09:10:48 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidspawn.c,v 1.2 2002/07/31 09:10:48 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pty.h>
#include <signal.h>

#include "pscommon.h"

#include "config_parsing.h"
#include "psidutil.h"

/* magic license check */
#include "../license/pslic_hidden.h"

#include "psidspawn.h"

static char errtxt[256];

int PSID_execv(const char *path, char *const argv[])
{
    int ret;
    int cnt;

    /* Try 5 times with delay 400ms = 2 sec overall */
    execv(path, argv);

    for (cnt=0; cnt<4; cnt++) {
	usleep(1000 * 400);
	ret = execv(path, argv);
    }

    return ret;
}

int PSID_execClient(PStask_t *task, int controlchannel)
{
    /* logging is done via the forwarder thru stderr! */
    struct stat sb;
    char pid_str[20];
    int i;

    /* change the gid */
    if (setgid(task->gid)<0) {
	char *errstr = strerror(errno);

	fprintf(stderr, "PSID_execClient(): setgid: %s",
		errstr ? errstr : "UNKNOWN");

	write(controlchannel, &errno, sizeof(errno));
	exit(0);
    }

    /* change the uid */
    if (setuid(task->uid)<0) {
	char *errstr = strerror(errno);

	fprintf(stderr, "PSID_execClient() setuid: %s",
		errstr ? errstr : "UNKNOWN");

	write(controlchannel, &errno, sizeof(errno));
	exit(0);
    }

    /* change to the appropriate directory */
    if (chdir(task->workingdir)<0) {
	char *errstr = strerror(errno);

	fprintf(stderr, "PSID_execClient(): chdir(%s): %s",
		task->workingdir ? task->workingdir : "",
		errstr ? errstr : "UNKNOWN");

	write(controlchannel, &errno, sizeof(errno));
	exit(0);
    }

    /* set some environment variables */
    setenv("PWD", task->workingdir, 1);

    if (task->environ) {
	for (i=0; task->environ[i]; i++) {
	    putenv(strdup(task->environ[i]));
	}
    }

    /* Test if executable is there */
    if (stat(task->argv[0], &sb) == -1) {
	char *errstr = strerror(errno);

	fprintf(stderr, "PSID_execClient(): stat(%s): %s",
		task->argv[0] ? task->argv[0] : "",
		errstr ? errstr : "UNKNOWN");

	write(controlchannel, &errno, sizeof(errno));
	exit(0);
    }

    if (!S_ISREG(sb.st_mode) || !(sb.st_mode & S_IXUSR)) {
	errno = EACCES;
	fprintf(stderr, "PSID_execClient(): stat(): %s",
		(!S_ISREG(sb.st_mode)) ? "S_ISREG error" :
		(sb.st_mode & S_IXUSR) ? "" : "S_IXUSR error");

	write(controlchannel, &errno, sizeof(errno));
	exit(0);
    }

    /* execute the image */
    if (PSID_execv(task->argv[0], &(task->argv[0]))<0) {
	char *errstr = strerror(errno);

	fprintf(stderr, "PSID_execClient() execv: %s",
		errstr ? errstr : "UNKNOWN");
    }
    /* never reached, if execv succesful */

    /*
     * send the forwarder a sign that the exec wasn't successful.
     * controlchannel would have been closed on successful exec.
     */
    write(controlchannel, &errno, sizeof(errno));
    exit(0);
}

int PSID_execForwarder(PStask_t *task, int controlchannel)
{
    pid_t pid;
    int clientfds[2], stdinfds[2], stdoutfds[2], stderrfds[2];
    int ret, buf, i;
    char *argv[9];

    PSID_blockSig(0, SIGCHLD);
    /* create a control channel in order to observe the client */
    if (pipe(clientfds)<0) {
	char *errstr = strerror(errno);

	snprintf(errtxt, sizeof(errtxt), "PSID_execForwarder(): pipe: %s ",
		 errstr ? errstr : "UNKNOWN");
	PSID_errlog(errtxt, 0);
    }
    fcntl(clientfds[1], F_SETFD, FD_CLOEXEC);

    /* create stdin/stdout/stderr connections between forwarder & client */
    /* don't echo within spawned client */
    task->termios.c_lflag &= ~(ECHO);
    /* first stdout */
    if (task->aretty & (1<<STDOUT_FILENO)) {
	if (openpty(&stdoutfds[0], &stdoutfds[1],
		    NULL, &task->termios, &task->winsize)) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "PSID_execForwarder() openpty(stdout): [%d] %s",
		     errno, errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);

	    write(controlchannel, &errno, sizeof(errno));
	    exit(1);
	}
    } else {
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, stdoutfds)) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "PSID_execForwarder() socketpair(stdout): [%d] %s",
		     errno, errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);

	    write(controlchannel, &errno, sizeof(errno));
	    exit(1);
	}
    }

    /* then stderr */
    if (task->aretty & (1<<STDERR_FILENO)) {
	if (openpty(&stderrfds[0], &stderrfds[1],
		    NULL, &task->termios, &task->winsize)) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "PSID_execForwarder() openpty(stderr): [%d] %s",
		     errno, errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);

	    write(controlchannel, &errno, sizeof(errno));
	    exit(1);
	}
    } else {
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, stderrfds)) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "PSID_execForwarder() socketpair(stderr): [%d] %s",
		     errno, errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);

	    write(controlchannel, &errno, sizeof(errno));
	    exit(1);
	}
    }

    /*
     * For stdin, use the stdout or stderr connection if the
     * requested type is available, or open an extra connection.
     */
    if (task->aretty & (1<<STDIN_FILENO)) {
	if (task->aretty & (1<<STDOUT_FILENO)) {
	    stdinfds[0] = stdoutfds[0];
	    stdinfds[1] = stdoutfds[1];
	} else if (task->aretty & (1<<STDERR_FILENO)) {
	    stdinfds[0] = stderrfds[0];
	    stdinfds[1] = stderrfds[1];
	} else {
	    if (openpty(&stdinfds[0], &stdinfds[1],
			NULL, &task->termios, &task->winsize)) {
		char *errstr = strerror(errno);
		snprintf(errtxt, sizeof(errtxt),
			 "PSID_execForwarder() openpty(stdin): [%d] %s",
			 errno, errstr ? errstr : "UNKNOWN");
		PSID_errlog(errtxt, 0);

		write(controlchannel, &errno, sizeof(errno));
		exit(1);
	    }
	}
    } else {
	if (!(task->aretty & (1<<STDOUT_FILENO))) {
	    stdinfds[0] = stdoutfds[0];
	    stdinfds[1] = stdoutfds[1];
	} else if (!(task->aretty & (1<<STDERR_FILENO))) {
	    stdinfds[0] = stderrfds[0];
	    stdinfds[1] = stderrfds[1];
	} else {
	    if (socketpair(PF_UNIX, SOCK_STREAM, 0, stdinfds)) {
		char *errstr = strerror(errno);
		snprintf(errtxt, sizeof(errtxt),
			 "PSID_execForwarder() socketpair(stdin): [%d] %s",
			 errno, errstr ? errstr : "UNKNOWN");
		PSID_errlog(errtxt, 0);

		write(controlchannel, &errno, sizeof(errno));
		exit(1);
	    }
	}
    }

    /* fork the client */
    if (!(pid = fork())) {
	/* this is the client process */

	/* close the reading pipe */
	close(clientfds[0]);

	/* close the master ttys / sockets */
        close(stdinfds[0]);
        close(stdoutfds[0]);
        close(stderrfds[0]);

	/* redirect input/output */
	errno = 0;
        dup2(stdinfds[1], STDIN_FILENO);
        dup2(stdoutfds[1], STDOUT_FILENO);
	dup2(stderrfds[1], STDERR_FILENO);

	/* From now on, all logging is done via the forwarder thru stderr */
	if (errno) {
	    /* at least one dup2() failed */
	    char *errstr = strerror(errno);
	    fprintf(stderr, "PSID_execForwarder() dup2(): [%d] %s",
		    errno, errstr ? errstr : "UNKNOWN");

	    write(clientfds[1], &errno, sizeof(errno));
	    exit(1);
	}

	/* close the now useless slave ttys / sockets */
        close(stdinfds[1]);
	close(stdoutfds[1]);
	close(stderrfds[1]);

	/* try to start the client */
	PSID_execClient(task, clientfds[1]);
    }

    /* this is the forwarder process */

    /* save errno in case of error */
    ret = errno;

    /* close the writing pipe */
    close(clientfds[1]);

    /* close the slave ttys / sockets */
    close(stdinfds[1]);
    close(stdoutfds[1]);
    close(stderrfds[1]);

    /* check if fork() was successful */
    if (pid == -1) {
	char *errstr = strerror(ret);

	close(clientfds[0]);

	snprintf(errtxt, sizeof(errtxt), "PSID_execForwarder() fork: %s",
		 errstr ? errstr : "UNKNOWN");
	PSID_errlog(errtxt, 0);

	write(controlchannel, &ret, sizeof(ret));
	exit(1);
    }

    /*
     * check for a sign of the client
     */
    snprintf(errtxt, sizeof(errtxt),
	     "PSID_execForwarder() waiting for my child (%d)", pid);
    PSID_errlog(errtxt, 10);

 restart:
    if ((ret=read(clientfds[0], &buf, sizeof(buf))) < 0) {
	if (errno == EINTR) {
	    goto restart;
	}
    }

    if (!ret) {
	/*
	 * the control channel was closed in case of a successful execv
	 */
	PSID_errlog("PSID_execForwarder() child execute was successful", 10);

	/* Tell the parent about the client's pid */
	buf = 0; /* errno will never be 0, this mark the following pid */
	write(controlchannel, &buf, sizeof(buf));
	buf = pid;
	write(controlchannel, &buf, sizeof(buf));
    } else {
	/*
	 * the child sent us a sign that the execv wasn't successful
	 */
	char *errstr = strerror(buf);

	snprintf(errtxt, sizeof(errtxt),
		 "PSID_execForwarder() child execute failed: %s",
		 errstr ? errstr : "UNKNOWN");
	PSID_errlog(errtxt, 0);

	/* Tell the parent about this */
	write(controlchannel, &buf, sizeof(buf));
	exit(1);
    }
    close(clientfds[0]);

    /* now really start the forwarder */
    argv[i=0] = (char*)malloc(strlen(PSC_lookupInstalldir()) + 20);
    sprintf(argv[i], "%s/bin/psiforwarder", PSC_lookupInstalldir());
    argv[++i] = (char*)malloc(10);
    sprintf(argv[i], "%u", task->loggernode);
    argv[++i] = (char*)malloc(10);
    sprintf(argv[i], "%d", task->loggerport);
    /* @todo loggernode/loggerport obsoleted by loggertid */
    argv[++i] = (char*)malloc(10);
    sprintf(argv[i], "%d", task->rank);
    /* @todo obsolete: get rank via INFO_request_taskinfo() within forwarder */
    argv[++i] = (char*)malloc(10);
    sprintf(argv[i], "%d", stdinfds[0]);
    argv[++i] = (char*)malloc(10);
    sprintf(argv[i], "%d", stdoutfds[0]);
    argv[++i] = (char*)malloc(10);
    sprintf(argv[i], "%d", stderrfds[0]);
    argv[++i] = (char*)malloc(10);
    sprintf(argv[i], "%d", pid);
    argv[++i] = NULL;

    PSID_execv(argv[0], argv);
    if (PSID_execv(task->argv[0], &(task->argv[0]))<0) {
	char *errstr = strerror(errno);

	snprintf(errtxt, sizeof(errtxt), "PSID_execForwarder() execv: %s",
		 errstr ? errstr : "UNKNOWN");
	PSID_errlog(errtxt, 0);
    }
    /* never reached, if execv succesful */

    /*
     * send the parent a sign that the exec wasn't successful.
     * controlchannel would have been closed on successful exec.
     */
    write(controlchannel, &errno, sizeof(errno));
    exit(1);
}

int PSID_spawnTask(PStask_t *forwarder, PStask_t *client)
{
    int forwarderfds[2];  /* pipe fds for communication with forwarder */
    int pid;              /* pid of the forwarder */
    int buf;              /* buffer for communication with forwarder */
    int i, ret;

    if (PSID_getDebugLevel() >= 10) {
	snprintf(errtxt, sizeof(errtxt), "PSID_taskspawn() task: ");
	PStask_snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
			client);
	PSID_errlog(errtxt, 10);
    }

    /* create a control channel in order to observe the forwarder */
    if (pipe(forwarderfds)<0) {
	char *errstr = strerror(errno);

	snprintf(errtxt, sizeof(errtxt), "PSID_spawnTask(): pipe: %s ",
		 errstr ? errstr : "UNKNOWN");
	PSID_errlog(errtxt, 0);
    }
    fcntl(forwarderfds[1], F_SETFD, FD_CLOEXEC);

    if (!lic_isvalid(&ConfigLicEnv)) {
    	PSID_errlog("Corrupted license!", 0);
	exit(1);
    }
    
    /* fork the forwarder */
    if (!(pid = fork())) {
	/* this is the forwarder process */

	/* close all fds except the control channel and stdin/stdout/stderr */
	for (i=0; i<getdtablesize(); i++) {
	    if (i!=STDIN_FILENO && i!=STDOUT_FILENO && i!=STDERR_FILENO
		&& i!=forwarderfds[1]) {
		close(i);
	    }
	}

	PSID_execForwarder(client, forwarderfds[1]);
    }

    /* this is the parent process */

    /* save errno in case of error */
    ret = errno;

    /* close the writing pipe */
    close(forwarderfds[1]);

    /* check if fork() was successful */
    if (pid == -1) {
	char *errstr = strerror(ret);

	close(forwarderfds[0]);

	snprintf(errtxt, sizeof(errtxt), "PSID_spawnTask() fork: %s",
		 errstr ? errstr : "UNKNOWN");
	PSID_errlog(errtxt, 0);

	return ret;
    }

    /*
     * check for a sign of the forwarder
     */
    snprintf(errtxt, sizeof(errtxt),
	     "PSID_spawnTask() waiting for my child (%d)", pid);
    PSID_errlog(errtxt, 10);

    forwarder->tid = PSC_getTID(-1, pid);
    client->tid = 0;

 restart:
    if ((ret=read(forwarderfds[0], &buf, sizeof(buf))) < 0) {
	if (errno == EINTR) {
	    goto restart;
	}
    }

    if (!ret) {
	if (client->tid) {
	    /*
	     * the control channel was closed in case of a successful execv
	     * after telling the client pid.
	     */
	    PSID_errlog("PSID_spawnTask() child execute was successful", 10);
	} else {
	    /*
	     * the control channel was closed without telling the client's pid.
	     */
	    PSID_errlog("PSID_spawnTask() haven't got child's pid", 0);

	    ret = EBADMSG;
	}
    } else {
	if (!buf) {
	    /*
	     * the forwarder want's to tell about the client's pid
	     */
	restart2:
	    if ((ret=read(forwarderfds[0], &buf, sizeof(buf))) < 0) {
		if (errno == EINTR) {
		    goto restart2;
		}
	    }
	    if (!ret) {
		/*
		 * the control channel was closed during message.
		 */
		PSID_errlog("PSID_spawnTask() channel closed unexpectedly", 0);

		ret = EBADMSG;
	    } else {
		client->tid = PSC_getTID(-1, buf);

		goto restart;
	    }
	} else {
	    /*
	     * the child sent us a sign that the execv wasn't successful
	     */
	    char *errstr = strerror(buf);

	    ret = buf;

	    snprintf(errtxt, sizeof(errtxt),
		     "PSID_spawnTask() child execute failed: %s",
		     errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);
	    
	}
    }

    close(forwarderfds[0]);

    return ret;
}
