/*
 *               ParaStation3
 * psidspawn.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidspawn.c,v 1.12 2003/08/04 15:00:09 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidspawn.c,v 1.12 2003/08/04 15:00:09 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pty.h>
#include <signal.h>
#include <syslog.h>

#include "pscommon.h"

#include "config_parsing.h"
#include "psidutil.h"
#include "psidforwarder.h"

/* magic license check */
#include "../license/pslic_hidden.h"

#include "psidspawn.h"

static char errtxt[256];

static char *get_strerror(int eno)
{
    char *ret = strerror(eno);
    if (ret) return ret; else return "UNKNOWN";
}

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

/* (workaround for automounter problems) */
int PSID_stat(char *file_name, struct stat *buf)
{
    int ret;
    int cnt;

    /* Try 5 times with delay 400ms = 2 sec overall */
    for (cnt=0;cnt<5;cnt++){
	ret = stat(file_name, buf);
	if (!ret) return 0; /* No error */
	usleep(1000 * 400);
    }
    return ret; /* return last error */
}


int PSID_execClient(PStask_t *task, int controlchannel)
{
    /* logging is done via the forwarder thru stderr! */
    struct stat sb;
    int i;

    /* change the gid */
    if (setgid(task->gid)<0) {
	fprintf(stderr, "%s: setgid: %s\n", __func__, get_strerror(errno));
	write(controlchannel, &errno, sizeof(errno));
	exit(0);
    }

    /* change the uid */
    if (setuid(task->uid)<0) {
	fprintf(stderr, "%s: setuid: %s\n", __func__, get_strerror(errno));
	write(controlchannel, &errno, sizeof(errno));
	exit(0);
    }

    /* change to the appropriate directory */
    if (chdir(task->workingdir)<0) {
	struct passwd *passwd;
	fprintf(stderr, "%s: chdir(%s): %s\n", __func__,
		task->workingdir ? task->workingdir : "", get_strerror(errno));
	fprintf(stderr, "Will use user's home directory\n");

	passwd = getpwuid(getuid());
	if (passwd) {
	    if (chdir(passwd->pw_dir)<0) {
		fprintf(stderr, "%s: chdir(%s): %s\n", __func__,
			passwd->pw_dir ? passwd->pw_dir : "",
			get_strerror(errno));
		write(controlchannel, &errno, sizeof(errno));
		exit(0);
	    }
	} else {
	    fprintf(stderr, "Cannot determine home directory\n");
	    write(controlchannel, &errno, sizeof(errno));
	    exit(0);
	}	    
    }

    /* set some environment variables */
    setenv("PWD", task->workingdir, 1);

    if (task->environ) {
	for (i=0; task->environ[i]; i++) {
	    putenv(strdup(task->environ[i]));
	}
    }

    /* Test if executable is there */
    /* @todo Why we do this? The execv will do the same later. *jh* */
    if (PSID_stat(task->argv[0], &sb) == -1) {
	fprintf(stderr, "%s: stat(%s): %s\n", __func__,
		task->argv[0] ? task->argv[0] : "", get_strerror(errno));
	write(controlchannel, &errno, sizeof(errno));
	exit(0);
    }

    if (!S_ISREG(sb.st_mode) || !(sb.st_mode & S_IXUSR)) {
	errno = EACCES;
	fprintf(stderr, "%s: stat(): %s\n", __func__,
		(!S_ISREG(sb.st_mode)) ? "S_ISREG error" :
		(sb.st_mode & S_IXUSR) ? "" : "S_IXUSR error");

	write(controlchannel, &errno, sizeof(errno));
	exit(0);
    }

    /* execute the image */
    if (PSID_execv(task->argv[0], &(task->argv[0]))<0) {
	fprintf(stderr, "%s: execv: %s", __func__, get_strerror(errno));
    }
    /* never reached, if execv succesful */

    /*
     * send the forwarder a sign that the exec wasn't successful.
     * controlchannel would have been closed on successful exec.
     */
    write(controlchannel, &errno, sizeof(errno));
    exit(0);
}

int PSID_execForwarder(PStask_t *task, int daemonfd, int controlchannel)
{
    pid_t pid;
    int clientfds[2], stdinfds[2], stdoutfds[2], stderrfds[2];
    int ret, buf;

    /* Block until the forwarder has handled all output */
    PSID_blockSig(1, SIGCHLD);

    /* create a control channel in order to observe the client */
    if (pipe(clientfds)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: pipe(): %s\n", __func__,
		 get_strerror(errno));
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
	    snprintf(errtxt, sizeof(errtxt), "%s: openpty(stdout): [%d] %s\n",
		     __func__, errno, get_strerror(errno));
	    PSID_errlog(errtxt, 0);

	    write(controlchannel, &errno, sizeof(errno));
	    exit(1);
	}
    } else {
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, stdoutfds)) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: socketpair(stdout): [%d] %s\n", __func__,
		     errno, get_strerror(errno));
	    PSID_errlog(errtxt, 0);

	    write(controlchannel, &errno, sizeof(errno));
	    exit(1);
	}
    }

    /* then stderr */
    if (task->aretty & (1<<STDERR_FILENO)) {
	if (openpty(&stderrfds[0], &stderrfds[1],
		    NULL, &task->termios, &task->winsize)) {
	    snprintf(errtxt, sizeof(errtxt), "%s: openpty(stderr): [%d] %s\n",
		     __func__, errno, get_strerror(errno));
	    PSID_errlog(errtxt, 0);

	    write(controlchannel, &errno, sizeof(errno));
	    exit(1);
	}
    } else {
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, stderrfds)) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: socketpair(stderr): [%d] %s\n", __func__,
		     errno, get_strerror(errno));
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
		snprintf(errtxt, sizeof(errtxt),
			 "%s: openpty(stdin): [%d] %s\n", __func__,
			 errno, get_strerror(errno));
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
		snprintf(errtxt, sizeof(errtxt),
			 "%s: socketpair(stdin): [%d] %s\n", __func__,
			 errno, get_strerror(errno));
		PSID_errlog(errtxt, 0);

		write(controlchannel, &errno, sizeof(errno));
		exit(1);
	    }
	}
    }

    /* fork the client */
    if (!(pid = fork())) {
	/* this is the client process */

	/*
	 * Create a new process group. This is needed since the daemon
	 * kills whole process groups. Otherwise the daemon might
	 * also kill the forwarder by sending a signal to the client.
	 */
	setpgid(0, 0);

	/* no direct connection to the daemon */
	close(daemonfd);

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
	    fprintf(stderr, "%s: dup2(): [%d] %s\n", __func__,
		    errno, get_strerror(errno));

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
	close(clientfds[0]);
	snprintf(errtxt, sizeof(errtxt), "%s: fork: %s\n", __func__,
		 get_strerror(errno));
	PSID_errlog(errtxt, 0);

	write(controlchannel, &ret, sizeof(ret));
	exit(1);
    }

    /*
     * check for a sign from the client
     */
    snprintf(errtxt, sizeof(errtxt), "%s: waiting for my child (%d)\n",
	     __func__, pid);
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
	snprintf(errtxt, sizeof(errtxt), "%s: child exec() ok\n", __func__);
	PSID_errlog(errtxt, 10);

	/* Tell the parent about the client's pid */
	buf = 0; /* errno will never be 0, this marks the following pid */
	write(controlchannel, &buf, sizeof(buf));
	buf = pid;
	write(controlchannel, &buf, sizeof(buf));
    } else {
	/*
	 * the child sent us a sign that the execv wasn't successful
	 */
	snprintf(errtxt, sizeof(errtxt), "%s: child exec() failed: %s\n",
		 __func__, get_strerror(errno));
	PSID_errlog(errtxt, 0);

	/* Tell the parent about this */
	buf=errno;
	write(controlchannel, &buf, sizeof(buf));
	exit(1);
    }
    close(clientfds[0]);

    closelog();

    /* reset all the signal handlers */
    signal(SIGINT   ,SIG_DFL);
    signal(SIGQUIT  ,SIG_DFL);
    signal(SIGILL   ,SIG_DFL);
    signal(SIGTRAP  ,SIG_DFL);
    signal(SIGABRT  ,SIG_DFL);
    signal(SIGIOT   ,SIG_DFL);
    signal(SIGBUS   ,SIG_DFL);
    signal(SIGFPE   ,SIG_DFL);
    signal(SIGUSR1  ,SIG_DFL);
    signal(SIGSEGV  ,SIG_DFL);
    signal(SIGUSR2  ,SIG_DFL);
    signal(SIGPIPE  ,SIG_DFL);
    signal(SIGTERM  ,SIG_DFL);
    signal(SIGCHLD  ,SIG_DFL);
    signal(SIGCONT  ,SIG_DFL);
    signal(SIGTSTP  ,SIG_DFL);
    signal(SIGTTIN  ,SIG_DFL);
    signal(SIGTTOU  ,SIG_DFL);
    signal(SIGURG   ,SIG_DFL);
    signal(SIGXCPU  ,SIG_DFL);
    signal(SIGXFSZ  ,SIG_DFL);
    signal(SIGVTALRM,SIG_DFL);
    signal(SIGPROF  ,SIG_DFL);
    signal(SIGWINCH ,SIG_DFL);
    signal(SIGIO    ,SIG_DFL);
#ifdef __osf__
    /* OSF on Alphas */
    signal(SIGSYS   ,SIG_DFL);
    signal(SIGINFO  ,SIG_DFL);
    signal(SIGIOINT ,SIG_DFL);
    signal(SIGAIO   ,SIG_DFL);
    signal(SIGPTY   ,SIG_DFL);
#elif defined(__alpha)
    /* Linux on Alpha*/
    signal( SIGSYS  ,SIG_DFL);
    signal( SIGINFO ,SIG_DFL);
#endif
#if !defined(__osf__) && !defined(__alpha)
    signal(SIGSTKFLT,SIG_DFL);
#endif

    /* Rename the syslog tag */
    openlog("psidforwarder", LOG_PID|LOG_CONS, ConfigLogDest);

    /* Release the waiting daemon and exec forwarder */
    close(controlchannel);
    PSID_forwarder(task, daemonfd, stdinfds[0], stdoutfds[0], stderrfds[0]);

    /* never reached */
    exit(1);
}

int PSID_spawnTask(PStask_t *forwarder, PStask_t *client)
{
    int forwarderfds[2];  /* pipe fds to control forwarders startup */
    int socketfds[2];     /* sockets for communication with forwarder */
    int pid;              /* pid of the forwarder */
    int buf;              /* buffer for communication with forwarder */
    int i, ret;

    if (PSID_getDebugLevel() >= 10) {
	snprintf(errtxt, sizeof(errtxt), "%s: task=", __func__);
	PStask_snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
			client);
	PSID_errlog(errtxt, 10);
    }

    /* create a control channel in order to observe the forwarder */
    if (pipe(forwarderfds)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: pipe(): %s\n",
		 __func__, get_strerror(errno));
	PSID_errlog(errtxt, 0);
	return errno;
    }
    fcntl(forwarderfds[1], F_SETFD, FD_CLOEXEC);

    /* create a socketpair for communication between daemon and forwarder */
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, socketfds)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: socketpair(): %s\n",
		 __func__, get_strerror(errno));
	PSID_errlog(errtxt, 0);
	close(forwarderfds[0]);
	close(forwarderfds[1]);
	return errno;
    }

    if (!lic_isvalid(&ConfigLicEnv)) {
    	PSID_errlog("Corrupted license!\n", 0);
	exit(1);
    }
    
    /* fork the forwarder */
    if (!(pid = fork())) {
	/* this is the forwarder process */

	/*
	 * Create a new process group. This is needed since the daemon
	 * kills whole process groups. Otherwise the daemon might
	 * commit suicide by sending signals to its clients.
	 */
	setpgid(0, 0);

	/* close all fds except the control channel, stdin/stdout/stderr and
	   the connecting socket */
	for (i=0; i<getdtablesize(); i++) {
	    if (i!=STDIN_FILENO && i!=STDOUT_FILENO && i!=STDERR_FILENO
		&& i!=forwarderfds[1] && i!=socketfds[1]) {
		close(i);
	    }
	}

	PSID_execForwarder(client, socketfds[1], forwarderfds[1]);
    }

    /* this is the parent process */

    /* save errno in case of error */
    ret = errno;

    /* close the writing pipe */
    close(forwarderfds[1]);

    /* close forwarders end of the socketpair */
    close(socketfds[1]);

    /* check if fork() was successful */
    if (pid == -1) {
	close(forwarderfds[0]);
	close(socketfds[0]);

	snprintf(errtxt, sizeof(errtxt), "%s: fork(): %s\n",
		 __func__, get_strerror(ret));
	PSID_errlog(errtxt, 0);

	return ret;
    }

    forwarder->tid = PSC_getTID(-1, pid);
    forwarder->fd = socketfds[0];
    /*
     * check for a sign of the forwarder
     */
    snprintf(errtxt, sizeof(errtxt), "%s: waiting for my child (%d)\n",
	     __func__, pid);
    PSID_errlog(errtxt, 10);

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
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: child %s spawned successfully\n",
		     __func__, PSC_printTID(client->tid));
	    PSID_errlog(errtxt, 10);
	} else {
	    /*
	     * the control channel was closed without telling the client's pid.
	     */
	    snprintf(errtxt, sizeof(errtxt), "%s: haven't got child's pid\n",
		     __func__);
	    PSID_errlog(errtxt, 0);

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
		snprintf(errtxt, sizeof(errtxt),
			 "%s: pipe closed unexpectedly\n", __func__);
		PSID_errlog(errtxt, 0);

		ret = EBADMSG;
	    } else {

		client->tid = PSC_getTID(-1, buf);

		goto restart;
	    }
	} else {
	    /*
	     * the child sent us a sign that the execv wasn't successful
	     */
	    ret = buf;
	    snprintf(errtxt, sizeof(errtxt), "%s: child exec() failed: %s\n",
		     __func__, get_strerror(buf));
	    PSID_errlog(errtxt, 0);
	    
	}
    }

    close(forwarderfds[0]);
    if (ret) close(socketfds[0]);

    return ret;
}
