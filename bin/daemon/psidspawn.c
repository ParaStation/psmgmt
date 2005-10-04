/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pty.h>
#include <signal.h>
#include <syslog.h>

#include "pscommon.h"

#include "psidutil.h"
#include "psidforwarder.h"

#include "psidspawn.h"

/**
 * @brief Get error string from errno.
 *
 * Create a error string describing the error marked by @a eno. @a eno
 * is the error number created by a recent failed system call and
 * returned within @a errno.
 *
 * The error string is either created using the strerror()
 * function. If this fails, i.e. the corresponding error number is
 * unknown to this function, the error string is set to "UNKNOWN".
 *
 * @return A pointer to a error description string is returned. This
 * string might also be "UNKNOWN".
 *
 * @see errno(3), strerror(3)
 */
static char *get_strerror(int eno)
{
    char *ret = strerror(eno);
    if (ret) return ret; else return "UNKNOWN";
}

/**
 * @brief Frontend to execv(3).
 *
 * Frontend to execv(3). Retry execv() on failure after a delay of
 * 400ms. With 5 tries at all this results in a total trial time of
 * about 2sec.
 *
 * @param path The pathname of the file to be executed.
 *
 * @param argv Array of pointers to null-terminated strings that
 * represent the argument list available to the new program. The first
 * argument, by convention, should point to the file name associated
 * with the file being executed. The array of pointers must be
 * terminated by a NULL pointer.
 *
 *
 * @return Like the execv(3) return value.
 *
 * @see execv(3)
 */
static int myexecv(const char *path, char *const argv[])
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

/**
 * @brief Frontend to stat(2).
 *
 * Frontend to stat(2). Retry stat() on failure after a delay of
 * 400ms. With 5 tries at all this results in a total trial time of
 * about 2sec.
 *
 * This is mainly a workaround for automounter problems.
 *
 * @param file_name The name of the file to stat. This might be a
 * absolute or relative path to the file.
 *
 * @param buf Buffer to hold the returned stat information of the file.
 *
 * @return Like the stat(2) return value.
 *
 * @see stat(2)
 */
static int mystat(char *file_name, struct stat *buf)
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


/**
 * @brief Actually start the client process.
 *
 * This function actually sets up the client process as described
 * within the task structure @a task.In order to do so, first the UID,
 * GID and the current working directory are set up correctly. If no
 * working directory is provided within @a task, the corresponding
 * user's home directory is used. After setting up the environment and
 * doing some tests on the existence and accessability of the
 * executable to call, finally the executable is called via @ref
 * myexecv().
 *
 * Since this function is typically called from within a fork()ed
 * process, possible errors cannot be signaled via a return
 * value. Thus a @a controllchannel is used, a file descriptor the
 * calling process has to listen to. If some data appears on this
 * channel, this is a signal that something failed during setting up
 * the client process. Actually the datum passed back is the current
 * @ref errno within the function set by the failing library call.
 *
 * Furthermore error-messages are sent to stderr. It is the task of
 * the calling function to provide an environment to forward this
 * message to the end-user of this function.
 *
 * @param task The task structure describing the client process to set
 * up.
 *
 * @param controlchannel
 *
 * @return No return value.
 *
 * @see fork(), errno
 */
static void execClient(PStask_t *task, int controlchannel)
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

    /* remove psid's group memberships */
    setgroups(0, NULL);

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
    if (mystat(task->argv[0], &sb) == -1) {
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
    if (myexecv(task->argv[0], &(task->argv[0]))<0) {
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

/**
 * @brief Reset signal handlers.
 *
 * Reset all the signal handlers.
 *
 * @return No return value.
 */
static void resetSignals(void)
{
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
#if defined(__alpha)
    /* Linux on Alpha*/
    signal( SIGSYS  ,SIG_DFL);
    signal( SIGINFO ,SIG_DFL);
#else
    signal(SIGSTKFLT,SIG_DFL);
#endif
}

/**
 * @brief @doctodo
 *
 * @todo Split into smaller parts, e.g. first, then part reuse.
 */
static int execForwarder(PStask_t *task, int daemonfd, int controlchannel)
{
    pid_t pid;
    int clientfds[2], stdinfds[2], stdoutfds[2], stderrfds[2];
    int ret, buf;

    /* Block until the forwarder has handled all output */
    PSID_blockSig(1, SIGCHLD);

    /* create a control channel in order to observe the client */
    if (pipe(clientfds)<0) {
	PSID_warn(-1, errno, "%s: pipe()", __func__);
    }
    fcntl(clientfds[1], F_SETFD, FD_CLOEXEC);

    /* create stdin/stdout/stderr connections between forwarder & client */
    /* don't echo within spawned client */
    task->termios.c_lflag &= ~(ECHO);
    /* first stdout */
    if (task->aretty & (1<<STDOUT_FILENO)) {
	if (openpty(&stdoutfds[0], &stdoutfds[1],
		    NULL, &task->termios, &task->winsize)) {
	    PSID_warn(-1, errno, "%s: openpty(stdout)", __func__);
	    write(controlchannel, &errno, sizeof(errno));
	    exit(1);
	}
    } else {
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, stdoutfds)) {
	    PSID_warn(-1, errno, "%s: socketpair(stdout)", __func__);
	    write(controlchannel, &errno, sizeof(errno));
	    exit(1);
	}
    }

    /* then stderr */
    if (task->aretty & (1<<STDERR_FILENO)) {
	if (openpty(&stderrfds[0], &stderrfds[1],
		    NULL, &task->termios, &task->winsize)) {
	    PSID_warn(-1, errno, "%s: openpty(stderr)", __func__);
	    write(controlchannel, &errno, sizeof(errno));
	    exit(1);
	}
    } else {
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, stderrfds)) {
	    PSID_warn(-1, errno, "%s: socketpair(stderr)", __func__);
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
		PSID_warn(-1, errno, "%s: openpty(stdin)", __func__);
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
		PSID_warn(-1, errno, "%s: socketpair(stdin)", __func__);
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
	execClient(task, clientfds[1]);
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
	PSID_warn(-1, errno, "%s: fork()", __func__);
	write(controlchannel, &ret, sizeof(ret));
	exit(1);
    }

    /* check for a sign from the client */
    PSID_log(PSID_LOG_SPAWN, "%s: waiting for my child (%d)\n", __func__, pid);

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
	PSID_log(PSID_LOG_SPAWN, "%s: child exec(): ok\n", __func__);

	/* Tell the parent about the client's pid */
	buf = 0; /* errno will never be 0, this marks the following pid */
	write(controlchannel, &buf, sizeof(buf));
	buf = pid;
	write(controlchannel, &buf, sizeof(buf));
    } else {
	/*
	 * the child sent us a sign that the execv wasn't successful
	 */
	char** av= task->argv;
	PSID_warn(-1, errno, "%s: child exec(\"%s\", argc=%d)", __func__,
		  av ? (av[0] ? av[0] : "argv[0]=<NULL>") : "<argv=NULL>",
		  task->argc);

	/* Tell the parent about this */
	buf=errno;
	write(controlchannel, &buf, sizeof(buf));
	exit(1);
    }
    close(clientfds[0]);

    closelog();

    resetSignals();

    /* Pass the client's PID to the forwarder. */
    task->tid = PSC_getTID(-1, pid);

    /* Rename the syslog tag */
    openlog("psidforwarder", LOG_PID|LOG_CONS, config->logDest);

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

    if (PSID_getDebugMask() & PSID_LOG_SPAWN) {
	char tasktxt[128];
	PStask_snprintf(tasktxt, sizeof(tasktxt), client);
	PSID_log(PSID_LOG_TASK, "%s: task=%s\n", __func__, tasktxt);
    }

    /* create a control channel in order to observe the forwarder */
    if (pipe(forwarderfds)<0) {
	PSID_warn(-1, errno, "%s: pipe()", __func__);
	return errno;
    }
    fcntl(forwarderfds[1], F_SETFD, FD_CLOEXEC);

    /* create a socketpair for communication between daemon and forwarder */
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, socketfds)<0) {
	PSID_warn(-1, errno, "%s: socketpair()", __func__);
	close(forwarderfds[0]);
	close(forwarderfds[1]);
	return errno;
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
		&& i!=forwarderfds[1] && i!=socketfds[1]
		&& i != PSID_lockFD) {
		close(i);
	    }
	}

	execForwarder(client, socketfds[1], forwarderfds[1]);
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

	PSID_warn(-1, errno, "%s: fork()", __func__);

	return ret;
    }

    forwarder->tid = PSC_getTID(-1, pid);
    forwarder->fd = socketfds[0];
    /* check for a sign of the forwarder */
    PSID_log(PSID_LOG_SPAWN, "%s: waiting for my child (%d)\n", __func__, pid);

    client->forwardertid = forwarder->tid;
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
	    PSID_log(PSID_LOG_SPAWN, "%s: child %s spawned successfully\n",
		     __func__, PSC_printTID(client->tid));
	} else {
	    /*
	     * the control channel was closed without telling the client's pid.
	     */
	    PSID_log(-1, "%s: haven't got child's pid\n", __func__);
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
		PSID_log(-1, "%s: pipe closed unexpectedly\n", __func__);
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
	    PSID_warn(-1, buf, "%s: child exec()", __func__);
	}
    }

    close(forwarderfds[0]);
    if (ret) close(socketfds[0]);

    return ret;
}
