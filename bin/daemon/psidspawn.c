/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psidspawn.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pty.h>
#include <pwd.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "list.h"
#include "pscio.h"
#include "pscommon.h"
#include "pscpu.h"
#include "psdaemonprotocol.h"
#include "psenv.h"
#include "psprotocolenv.h"
#include "psreservation.h"
#include "psserial.h"
#include "psstrv.h"

#include "timer.h"

#include "psidclient.h"
#include "psidcomm.h"
#include "psidforwarder.h"
#include "psidhook.h"
#include "psidnodes.h"
#include "psidpin.h"
#include "psidsignal.h"
#include "psidstatus.h"
#include "psidtask.h"
#include "psidutil.h"

/** File-descriptor used by the alarm-handler to write its errno */
static int alarmFD = -1;

/** Function interrupted. This will be reported by the alarm-handler */
static const char *alarmFunc = NULL;

/**
 * @brief Alarm handler
 *
 * Handles expired alarms. This might happen due to hanging
 * file-systems during spawn of new processes.
 *
 * @param sig Signal to be handled. Should always by SIGALRM.
 *
 * @return No return value
 */
static void alarmHandler(int sig)
{
    int eno = ETIME;

    if (!alarmFunc) alarmFunc = "UNKNOWN";

    PSID_fwarn(eno, "%s()", alarmFunc);
    fprintf(stderr, "%s: %s(): %s\n", __func__, alarmFunc, strerror(eno));

    if (alarmFD >= 0) {
	int ret = write(alarmFD, &eno, sizeof(eno));
	if (ret < 0) {
	    eno = errno;
	    PSID_fwarn(eno, "write()");
	    fprintf(stderr, "%s: write(): %s\n", __func__, strerror(eno));
	}
    }

    exit(1);
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
static int myexecv(const char *path, strv_t argV)
{
    char **argv = strvGetArray(argV);

    /* Try 5 times with delay 400ms = 2 sec overall */
    execv(path, argv);

    int ret;
    for (int cnt = 0; cnt < 4; cnt++) {
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
    int cnt = PSIDnodes_maxStatTry(PSC_getMyID()), ret;

    /* Try several times with delay 400ms */
    do {
	ret = stat(file_name, buf);
	if (!ret) return 0; /* No error */
	usleep(1000 * 400);
    } while (--cnt > 0);

    return ret; /* return last error */
}

static void pty_setowner(uid_t uid, gid_t gid, const char *tty)
{
    struct group *grp;
    mode_t mode;
    struct stat st;

    /* Determine the group to make the owner of the tty. */
    grp = getgrnam("tty");
    if (grp) {
	gid = grp->gr_gid;
	mode = S_IRUSR | S_IWUSR | S_IWGRP;
    } else {
	mode = S_IRUSR | S_IWUSR | S_IWGRP | S_IWOTH;
    }

    /*
     * Change owner and mode of the tty as required.
     * Warn but continue if filesystem is read-only and the uids match/
     * tty is owned by root.
     */
    if (stat(tty, &st)) PSID_exit(errno, "%s: stat(%s)", __func__, tty);

    if (st.st_uid != uid || st.st_gid != gid) {
	if (chown(tty, uid, gid) < 0) {
	    if (errno == EROFS && (st.st_uid == uid || st.st_uid == 0)) {
		PSID_fwarn(errno, "chown(%s, %u, %u)", tty, uid, gid);
	    } else {
		PSID_exit(errno, "%s: chown(%s, %u, %u)", __func__,
			  tty, (u_int)uid, (u_int)gid);
	    }
	}
    }

    if ((st.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO)) != mode) {
	if (chmod(tty, mode) < 0) {
	    if (errno == EROFS && (st.st_mode & (S_IRGRP | S_IROTH)) == 0) {
		PSID_fwarn(errno, "chmod(%s, 0%o)", tty, (u_int)mode);
	    } else {
		PSID_exit(errno, "%s: chmod(%s, 0%o)", __func__,
			  tty, (u_int)mode);
	    }
	}
    }
}

#define _PATH_TTY "/dev/tty"

/* Makes the tty the process's controlling tty and sets it to sane modes. */
static void pty_make_controlling_tty(int *ttyfd, const char *tty)
{
    int fd;
    void *oldCONT, *oldHUP;

    /* First disconnect from the old controlling tty. */
    fd = open(_PATH_TTY, O_RDWR | O_NOCTTY);
    if (fd >= 0) {
	if (ioctl(fd, TIOCNOTTY, NULL) < 0)
	    PSID_fwarn(errno, "ioctl(TIOCNOTTY)");
	close(fd);
    }

    /*
     * Verify that we are successfully disconnected from the controlling
     * tty.
     */
    fd = open(_PATH_TTY, O_RDWR | O_NOCTTY);
    if (fd >= 0) {
	PSID_flog("still connected to controlling tty\n");
	close(fd);
    }

    /* Make it our controlling tty. */
#ifdef TIOCSCTTY
    if (ioctl(*ttyfd, TIOCSCTTY, NULL) < 0)
	PSID_fwarn(errno, "ioctl(TIOCSCTTY)");
#else
#error No TIOCSCTTY
#endif /* TIOCSCTTY */

    oldCONT = PSC_setSigHandler(SIGCONT, SIG_IGN);
    oldHUP = PSC_setSigHandler(SIGHUP, SIG_IGN);
    if (vhangup() < 0) PSID_fwarn(errno, "vhangup()");
    PSC_setSigHandler(SIGCONT, oldCONT);
    PSC_setSigHandler(SIGHUP, oldHUP);

    fd = open(tty, O_RDWR);
    if (fd < 0) {
	PSID_fwarn(errno, "open(%s)", tty);
    } else {
	close(*ttyfd);
	*ttyfd = fd;
    }
    /* Verify that we now have a controlling tty. */
    fd = open(_PATH_TTY, O_WRONLY);
    if (fd < 0) {
	PSID_fwarn(errno, "open(%s)", _PATH_TTY);
	PSID_flog("unable to set controlling tty: %s\n", _PATH_TTY);
    } else {
	close(fd);
    }
}

/**
 * @brief Change into working directory
 *
 * Try to change into the client-task's @a task working directory. If
 * this fails, changing into the corresponding user's home-directory
 * is attempted.
 *
 * @param task Structure describing the client-task
 *
 * @return Upon success true is returned; if an error occurred, false
 * is returned and errno is set accordingly
 */
static bool changeToWorkDir(PStask_t *task)
{
    alarmFunc = __func__;

    if (chdir(task->workingdir) == 0) return true;

    int eno = errno;
    char *rawIO = getenv("__PSI_RAW_IO");
    if (!rawIO) {
	fprintf(stderr, "%s: chdir(%s): %s\n", __func__,
		task->workingdir ? task->workingdir : "", strerror(eno));
	fprintf(stderr, "Will use user's home directory\n");
    }

    char *pwBuf = NULL;
    struct passwd *passwd = PSC_getpwuidBuf(task->uid, &pwBuf);
    if (!passwd) {
	if (rawIO) {
	    PSID_log("cannot determine home directory\n");
	} else {
	    fprintf(stderr, "Cannot determine home directory\n");
	}
	errno = ENOENT;
	return false;
    }

    int ret = chdir(passwd->pw_dir);
    eno = errno;
    free(pwBuf);
    if (!ret) return true;

    if (rawIO) {
	PSID_fwarn(eno, "chdir(%s)", passwd->pw_dir ? passwd->pw_dir : "");
    } else {
	fprintf(stderr, "%s: chdir(%s): %s\n", __func__,
		passwd->pw_dir ? passwd->pw_dir : "", strerror(eno));
    }
    errno = eno;
    return false;
}

/**
 * @brief Test if tasks executable is there
 *
 * Test if the child-task's @a task executable is available. If the
 * task's argv[0] contains an absolute path, only this is
 * search. Otherwise all paths listed in the PATH environment
 * variables are searched for the executable.
 *
 * If the executable is found, it's accessibility is tested.
 *
 * If all tests are passed, task's argv[0] is returned. Future calls
 * to execv() might get this pointer as the first argument.
 *
 * @param task Structure describing the client-task.
 *
 * @return Upon success, task's argv[0] (different from NULL) is
 * returned; if an error occurred, NULL is returned and errno is set
 * accordingly
 */
static char *testExecutable(PStask_t *task)
{
    char buf[64];

    alarmFunc = __func__;
    char *cmd = strvGet(task->argV, 0);
    if (!cmd) {
	fprintf(stderr, "No command given!\n");
	errno = ENOENT;
	return NULL;
    }

    if (!strcmp(cmd, "$SHELL")) {
	char *pwBuf = NULL;
	struct passwd *passwd = PSC_getpwuidBuf(task->uid, &pwBuf);
	if (!passwd) {
	    int eno = errno;
	    fprintf(stderr, "%s: Unable to determine $SHELL: %s\n", __func__,
		    strerror(eno));
	    errno = eno;
	    return NULL;
	}
	strvReplace(task->argV, 0, passwd->pw_shell);
	cmd = strvGet(task->argV, 0);
	free(pwBuf);
    }

    /* Test if executable is there */
    bool execFound = false;
    struct stat sb;
    if (cmd[0] != '/' && cmd[0] != '.') {
	/* Relative path -> let's search in $PATH */
	char *p = getenv("PATH");

	if (!p) {
	    fprintf(stderr, "No path?\n");
	} else {
	    char *path = strdup(p);
	    p = strtok(path, ":");
	    while (p) {
		char *fn = PSC_concat(p, "/", cmd);

		if (!stat(fn, &sb) && (sb.st_mode & S_IXUSR)
		    && !S_ISDIR(sb.st_mode) ) {
		    strvReplace(task->argV, 0, fn);
		    cmd = strvGet(task->argV, 0);
		    free(fn);
		    execFound = true;
		    break;
		}

		free(fn);
		p = strtok(NULL, ":");
	    }
	    free(path);
	}
    }

    if (!execFound) {
	/* this might be on NFS -> use mystat */
	if (mystat(cmd, &sb) == -1) {
	    int eno = errno;
	    fprintf(stderr, "%s: stat(%s): %s\n", __func__, cmd, strerror(eno));
	    errno = eno;
	    return NULL;
	}
    }

    if (!S_ISREG(sb.st_mode) || !(sb.st_mode & S_IXUSR)) {
	fprintf(stderr, "%s: stat(%s): %s\n", __func__, cmd,
		(!S_ISREG(sb.st_mode)) ? "S_ISREG error" :
		(sb.st_mode & S_IXUSR) ? "" : "S_IXUSR error");
	errno = EACCES;
	return NULL;
    }

    /* Try to read first 64 bytes; on Lustre this might hang */
    int fd = open(cmd, O_RDONLY);
    if (fd < 0) {
	int eno = errno;
	if (eno != EACCES) { /* Might not have to permission to read (#1058) */
	    fprintf(stderr, "%s: open(): %s\n", __func__, strerror(eno));
	    errno = eno;
	    return NULL;
	}
    } else if (PSCio_recvBuf(fd, buf, sizeof(buf)) < 0) {
	int eno = errno;
	fprintf(stderr, "%s: PSCio_recvBuf(): %s\n", __func__, strerror(eno));
	errno = eno;
	return NULL;
    } else {
	close(fd);
    }

    /* shells */
    if (!strcmp(cmd, "/bin/bash")) {
	cmd = strdup(cmd);
	if (strvSize(task->argV) == 2 && !strcmp(strvGet(task->argV, 1), "-i")) {
	    strvDestroy(task->argV);
	    task->argV = strvNew(NULL);
	    strvAdd(task->argV, "-bash");
	} else {
	    strvReplace(task->argV, 0, "bash");
	}
    } else if (!strcmp(cmd, "/bin/tcsh")) {
	cmd = strdup(cmd);
	if (strvSize(task->argV) == 2 && !strcmp(strvGet(task->argV, 1), "-i")) {
	    strvDestroy(task->argV);
	    task->argV = strvNew(NULL);
	    strvAdd(task->argV, "-tcsh");
	} else {
	    strvReplace(task->argV, 0, "tcsh");
	}
    }

    return cmd;
}

static void restoreLimits(void)
{
    for (int i = 0; PSP_rlimitEnv[i].envName; i++) {
	struct rlimit rlim;
	char *envStr = getenv(PSP_rlimitEnv[i].envName);

	if (!envStr) continue;

	getrlimit(PSP_rlimitEnv[i].resource, &rlim);
	if (!strcmp("infinity", envStr)) {
	    rlim.rlim_cur = rlim.rlim_max;
	} else {
	    int ret = sscanf(envStr, "%lx", &rlim.rlim_cur);
	    if (ret < 1) rlim.rlim_cur = 0;
	    rlim.rlim_cur =
		(rlim.rlim_max > rlim.rlim_cur) ? rlim.rlim_cur : rlim.rlim_max;
	}
	setrlimit(PSP_rlimitEnv[i].resource, &rlim);
    }
}

/**
 * @brief Actually start the client process.
 *
 * This function actually sets up the client process as described
 * within the task structure @a task. In order to do so, first the
 * UID, GID and the current working directory are set up correctly. If
 * no working directory is provided within @a task, the corresponding
 * user's home directory is used. After some tests on the existence
 * and accessibility of the executable to call, the forwarder is
 * signaled via the control-channel within task->fd to register the
 * child process within the local daemon. As soon as the forwarder
 * acknowledges the registration finally the executable is called via
 * @ref myexecv().
 *
 * Since this function is typically called from within a fork()ed
 * process, possible errors cannot be signaled via a return
 * value. Thus the control-channel within task->fd is used, a file
 * descriptor building one end of a pipe. The calling process has to
 * listen to the other end. If some data appears on this channel, it
 * is a strong signal that something failed during setting up the
 * client process. Actually the datum passed back is the current @ref
 * errno within the function set by the failing library call.
 *
 * Further error-messages are sent to stderr. It is the task of
 * the calling function to provide an environment to forward this
 * message to the end-user of this function.
 *
 * @param task The task structure describing the client process to be
 * set up.
 *
 * @return No return value.
 *
 * @see fork(), errno
 */
__attribute__ ((noreturn))
static void execClient(PStask_t *task)
{
    /* logging is done via the forwarder thru stderr! */

    /* Give the client some hint where we are running (#2911) */
    char nodeIDStr[16];
    snprintf(nodeIDStr, sizeof(nodeIDStr), "%hd", PSC_getMyID());
    setenv("PSP_SMP_NODE_ID", nodeIDStr, 1);

    /* change the gid; exit() on failure */
    if (setgid(task->gid) < 0) {
	int eno = errno;
	fprintf(stderr, "%s: setgid: %s\n", __func__, strerror(eno));
	if (write(task->fd, &eno, sizeof(eno)) < 0) {
	    eno = errno;
	    fprintf(stderr, "%s: setgid: write(): %s\n", __func__,
		    strerror(eno));
	}
	exit(1);
    }

    /* remove psid's group memberships */
    setgroups(0, NULL);

    /* try to set supplementary groups if requested; failure is ignored */
    if (PSIDnodes_supplGrps(PSC_getMyID())) {
	char *name = PSC_userFromUID(task->uid);
	if (name && initgroups(name, task->gid) < 0) {
	    fprintf(stderr, "%s: initgroups(): %s\n", __func__,
		    strerror(errno));
	}
	free(name);
    }

    /* change the uid; exit() on failure */
    if (setuid(task->uid) < 0) {
	int eno = errno;
	fprintf(stderr, "%s: setuid: %s\n", __func__, strerror(eno));
	if (write(task->fd, &eno, sizeof(eno)) < 0) {
	    eno = errno;
	    fprintf(stderr, "%s: setuid: write(): %s\n", __func__,
		    strerror(eno));
	}
	exit(1);
    }

    /* re-enable capability to create coredumps */
    prctl(PR_SET_DUMPABLE, 1);

    /* restore various resource limits */
    restoreLimits();

    /* restore umask settings */
    char *envStr = getenv("__PSI_UMASK");
    if (envStr) {
	mode_t mask;
	if (sscanf(envStr, "%o", &mask) > 0) umask(mask);
    }

    /* used e.g. by psslurm Spank to modify the namespace,
     * thus before testExecutable() and alarm */
    if (PSIDhook_call(PSIDHOOK_EXEC_CLIENT_PREP, task) < 0) {
	int eno = EPERM;
	fprintf(stderr, "%s: PSIDHOOK_EXEC_CLIENT_PREP failed\n", __func__);
	if (write(task->fd, &eno, sizeof(eno)) < 0) {
	    eno = errno;
	    fprintf(stderr, "%s: PSIDHOOK_EXEC_CLIENT_PREP: write(): %s\n",
		    __func__, strerror(eno));
	}
	exit(1);
    }

    /* setup alarm */
    alarmFD = task->fd;
    PSC_setSigHandler(SIGALRM, alarmHandler);
    int timeout = 30;
    envStr = getenv("__PSI_ALARM_TMOUT");
    if (envStr) {
	int tmout;
	if (sscanf(envStr, "%d", &tmout) > 0) timeout = tmout;
    }
    alarm(timeout);

    if (!changeToWorkDir(task)) {
	int eno = errno;
	if (write(task->fd, &eno, sizeof(eno)) < 0) {
	    eno = errno;
	    fprintf(stderr, "%s: changeToWorkDir: write(): %s\n", __func__,
		    strerror(eno));
	}
	exit(1);
    }

    char *executable = testExecutable(task);
    if (!executable) {
	int eno = errno;
	if (write(task->fd, &eno, sizeof(eno)) < 0) {
	    eno = errno;
	    fprintf(stderr, "%s: testExecutable: write(): %s\n", __func__,
		    strerror(eno));
	}
	exit(1);
    }
    alarm(0);

    /* reset handling of SIGALRM */
    PSC_setSigHandler(SIGALRM, SIG_DFL);

    PSIDpin_doClamps(task);

    /* Signal forwarder we're ready for execve() */
    int forwErr = 0;
    if (write(task->fd, &forwErr, sizeof(forwErr)) < 0) {
	int eno = errno;
	fprintf(stderr, "%s: write(): %s\n", __func__, strerror(eno));
	PSID_exit(eno, "%s: write(%d)", __func__, task->fd);
    }

    if (PSCio_recvBuf(task->fd, &forwErr, sizeof(forwErr)) < 0) {
	int eno = errno;
	fprintf(stderr, "%s: PSCio_recvBuf: %s\n", __func__, strerror(eno));
	PSID_exit(eno, "%s: PSCio_recvBuf", __func__);
    } else {
	close(task->fd);
    }

    if (forwErr) {
	fprintf(stderr, "%s: DD_CHILDBORN failed\n", __func__);
	PSID_flog("DD_CHILDBORN failed\n");
	exit(1);
    }

    /* used by psslurm to modify default pinning; thus after doClamps() */
    if (PSIDhook_call(PSIDHOOK_EXEC_CLIENT_USER, task) < 0) {
	int eno = EPERM;
	fprintf(stderr, "%s: PSIDHOOK_EXEC_CLIENT_USER failed\n", __func__);
	if (write(task->fd, &eno, sizeof(eno)) < 0) {
	    eno = errno;
	    fprintf(stderr, "%s: PSIDHOOK_EXEC_CLIENT_USER: write(): %s\n",
		    __func__, strerror(eno));
	}
	exit(1);
    }

    /* Finally, unblock SIGCHLD since it might be unexpected for the client */
    PSID_blockSig(SIGCHLD, false);

    /* used by psslurm to execute the child in a container */
    if (PSIDhook_call(PSIDHOOK_EXEC_CLIENT_EXEC, task) < 0) {
	int eno = EPERM;
	fprintf(stderr, "%s: PSIDHOOK_EXEC_CLIENT_EXEC failed\n", __func__);
	if (write(task->fd, &eno, sizeof(eno)) < 0) {
	    eno = errno;
	    fprintf(stderr, "%s: PSIDHOOK_EXEC_CLIENT_EXEC: write(): %s\n",
		    __func__, strerror(eno));
	}
	exit(1);
    }

    /* execute the image */
    if (myexecv(executable, task->argV) < 0) {
	fprintf(stderr, "%s: execv(%s): %m\n", __func__, executable);
	PSID_exit(errno, "%s: execv(%s)", __func__, executable);
    }

    /* never reached, if execv succesful */
    exit(2);
}

/**
 * @brief Create fd pair
 *
 * Create a pair of file-descriptors @a fds acting as forwarder's
 * stdout or stderr connections to an actual client. The task
 * structure @a task determines in the member @a aretty if openpty()
 * or socketpair() is used to create the file-descriptor-pair. @a
 * fileNo is either STDOUT_FILENO or STDERR_FILENO and steers creation
 * of the corresponding channels.
 *
 * If any error occurs during this process, the corresponding errno
 * is returned.
 *
 * @param task Task structure determining the use of either openpty()
 * or socketpair().
 *
 * @param fds The pair of file-descriptors to create.
 *
 * @param fileNo Either STDOUT_FILENO or STDERR_FILENO.
 *
 * @return Upon success, 0 is returned. Otherwise a value different
 * from 0 is returned representing an errno.
 *
 * @see openpty(), socketpair()
 */
static int openChannel(PStask_t *task, int *fds, int fileNo)
{
    char *fdName = (fileNo == STDOUT_FILENO) ? "stdout" :
	(fileNo == STDERR_FILENO) ? "stderr" :
	(fileNo == STDIN_FILENO) ? "stdin" : "unknown";

    if (task->aretty & (1<<fileNo)) {
	if (openpty(&fds[0], &fds[1], NULL, &task->termios, &task->winsize)) {
	    int eno = errno;
	    PSID_fwarn(eno, "openpty(%s)", fdName);
	    return eno;
	}
    } else {
	/* need to create as user to grant permission to access /dev/stdX */
	if (seteuid(task->uid) == -1) {
	    int eno = errno;
	    PSID_fwarn(eno, "seteiud(%i)", task->uid);
	    return eno;
	}
	if (pipe(fds) == -1) {
	    int eno = errno;
	    PSID_fwarn(eno, "pipe(%s)", fdName);
	    return eno;
	}
	if (seteuid(0) == -1) {
	    int eno = errno;
	    PSID_fwarn(eno, "seteiud(0)");
	    return eno;
	}
    }

    return 0;
}

/**
 * @brief Log /proc/@a pid/stat
 *
 * Try to read content of the file /proc/<pid>/stat for <pid> @a pid
 * and log it to psid's logging facility.
 *
 * @param pid Process ID of the process to stat
 *
 * @return No return value
 */
static void statPID(pid_t pid)
{
    FILE *statFile;
    struct stat sbuf;
    char fileName[128], *statLine = NULL;
    size_t len;

    snprintf(fileName, sizeof(fileName), "/proc/%i/stat", pid);
    if (stat(fileName, &sbuf) == -1) {
	PSID_fwarn(errno, "PID %d: stat()", pid);
	return;
    }

    statFile = fopen(fileName,"r");
    if (!statFile) {
	PSID_fwarn(errno, "PID %d: fopen(%s)", pid, fileName);
	return;
    }

    if (getline(&statLine, &len, statFile) < 0) {
	PSID_fwarn(errno, "PID %d: getline(%s)", pid, fileName);
	return;
    }

    PSID_flog("PID %d: %s\n", pid, statLine);
    free(statLine);
}

/**
 * @brief Create forwarder sandbox
 *
 * This function sets up a forwarder sandbox. Afterwards @ref
 * execClient() is called in order to start a client process within
 * this sandbox. The client process is described within the task
 * structure @a task.
 *
 * The forwarder is connected to the local daemon from the very
 * beginning, i.e. it is not reconnecting. For this one end of a
 * socketpair of type UNIX stream is used in order to setup this
 * connection. The corresponding file descriptor has to be passed
 * within the @a daemonfd argument.
 *
 * Since this function is typically called from within a fork()ed
 * process, possible errors cannot be signaled via a return
 * value. Thus the control-channel @a cntrlCh is used, a file
 * descriptor building one end of a pipe. The calling process has to
 * listen to the other end. If some data appears on this channel, it
 * is a strong signal that something failed during setting up the
 * client process. Actually the datum passed back is the current @ref
 * errno within the function set by the failing library call.
 *
 * Furthermore error-messages are sent to stderr. It is the task of
 * the calling function to provide an environment to forward this
 * message to the end-user of this function.
 *
 * @param daemonfd File descriptor representing one end of a UNIX
 * stream socketpair used as a connection to the local daemon
 *
 * @param task Task structure describing the client process to set
 * up
 *
 * @return No return value.
 *
 * @see fork(), errno
 */
static void execForwarder(PStask_t *task)
{
    int stdinfds[2] = {-1, -1}, stdoutfds[2] = {-1, -1};
    int stderrfds[2] = {-1, -1}, controlfds[2] = {-1, -1};
    int eno = 0;
    char *envStr;
    struct timeval start, end = { .tv_sec = 0, .tv_usec = 0 }, stv;
    struct timeval timeout = { .tv_sec = 30, .tv_usec = 0};

    /* setup the environment; done here to pass it to forwarder, too */
    setenv("PWD", task->workingdir, 1);

    for (char **e = envGetArray(task->env); e && *e; e++) putenv(strdup(*e));

    /* create a socketpair for communication between forwarder and client */
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, controlfds) < 0) {
	eno = errno;
	PSID_fwarn(eno, "socketpair()");
	goto error;
    }

    if (task->aretty & (1<<STDIN_FILENO)
	&& task->aretty & (1<<STDOUT_FILENO)
	&& task->aretty & (1<<STDERR_FILENO)) task->interactive = true;

    /* create stdin/stdout/stderr connections between forwarder & client */
    if (task->interactive) {
	if ((eno = openChannel(task, stderrfds, STDERR_FILENO))) goto error;
    } else {
	/* first stdout */
	if ((eno = openChannel(task, stdoutfds, STDOUT_FILENO))) goto error;

	/* then stderr */
	if ((eno = openChannel(task, stderrfds, STDERR_FILENO))) goto error;

	/* last stdin */
	if ((eno = openChannel(task, stdinfds, STDIN_FILENO))) goto error;
    }

    /* Ensure processes use correct loginuid */
    PSID_adjustLoginUID(task->uid);

    /* init the process manager sockets */
    if (PSIDhook_call(PSIDHOOK_EXEC_FORWARDER, task) == -1) {
	eno = EINVAL;
	goto error;
    }

    /* Jail all my children */
    pid_t pid = getpid();
    if (PSIDhook_call(PSIDHOOK_JAIL_CHILD, &pid) < 0) {
	PSID_flog("hook PSIDHOOK_JAIL_CHILD failed\n");
	eno = EINVAL;
	goto error;
    }

    /* fork the client */
    if (!(pid = fork())) {
	/* this is the client process */
	/* no direct connection to the daemon */
	close(task->fd);

	/* prepare connection to forwarder */
	task->fd = controlfds[1];
	close(controlfds[0]);

	/* Reset connection to syslog */
	closelog();
	openlog("psid_client", LOG_PID|LOG_CONS, PSID_config->logDest);

	/*
	 * Create a new process group. This is needed since the daemon
	 * kills whole process groups. Otherwise the daemon might
	 * also kill the forwarder by sending a signal to the client.
	 */
	if (setsid() < 0) PSID_fwarn(errno, "setsid()");

	/* close the master ttys / sockets */
	if (task->interactive) {
	    char *name = ttyname(stderrfds[1]);

	    close(stderrfds[0]);
	    task->stderr_fd = stderrfds[1];

	    /* prepare the pty */
	    pty_setowner(task->uid, task->gid, name);
	    pty_make_controlling_tty(&task->stderr_fd, name);
	    tcsetattr(task->stderr_fd, TCSANOW, &task->termios);
	    (void) ioctl(task->stderr_fd, TIOCSWINSZ, &task->winsize);

	    /* stdin/stdout/stderr share one PTY */
	    task->stdin_fd = task->stderr_fd;
	    task->stdout_fd = task->stderr_fd;
	} else {
	    close(stdinfds[1]);
	    task->stdin_fd = stdinfds[0];
	    if (task->aretty & (1<<STDIN_FILENO)) {
		char *name = ttyname(stdinfds[0]);
		pty_setowner(task->uid, task->gid, name);
	    }

	    close(stdoutfds[0]);
	    task->stdout_fd = stdoutfds[1];
	    if (task->aretty & (1<<STDOUT_FILENO)) {
		char *name = ttyname(stdoutfds[1]);
		pty_setowner(task->uid, task->gid, name);
	    }

	    close(stderrfds[0]);
	    task->stderr_fd = stderrfds[1];
	    if (task->aretty & (1<<STDERR_FILENO)) {
		char *name = ttyname(stderrfds[1]);
		pty_setowner(task->uid, task->gid, name);
	    }
	}

	/* redirect stdin/stdout/stderr */
	if (dup2(task->stderr_fd, STDERR_FILENO) < 0) {
	    eno = errno;
	    if (write(task->fd, &eno, sizeof(eno)) < 0) {
		PSID_fwarn(errno, "dup2(stderr): write()");
	    }
	    PSID_exit(eno, "%s: dup2(stderr)", __func__);
	}

	/* From now on, all logging is done via the forwarder thru stderr */

	if (dup2(task->stdin_fd, STDIN_FILENO) < 0) {
	    eno = errno;
	    fprintf(stderr, "%s: dup2(stdin): %s\n", __func__, strerror(eno));
	    if (write(task->fd, &eno, sizeof(eno)) < 0) {
		PSID_fwarn(errno, "dup2(stdin): write()");
	    }
	    PSID_exit(eno, "%s: dup2(stdin)", __func__);
	}
	if (dup2(task->stdout_fd, STDOUT_FILENO) < 0) {
	    eno = errno;
	    fprintf(stderr, "%s: dup2(stdout): %s\n", __func__, strerror(eno));
	    if (write(task->fd, &eno, sizeof(eno)) < 0) {
		PSID_fwarn(errno, "dup2(stdout): write()");
	    }
	    PSID_exit(eno, "%s: dup2(stdout)", __func__);
	}

	/* close the now useless slave ttys / sockets */
	close(task->stderr_fd);
	if (!task->interactive) {
	    close(task->stdin_fd);
	    close(task->stdout_fd);
	}

	/* counterpart to PSIDHOOK_EXEC_FORWARDER */
	PSIDhook_call(PSIDHOOK_EXEC_CLIENT, task);

	/* try to start the client */
	unsetenv("SSS_NSS_USE_MEMCACHE");
	execClient(task);

	/* Never be here */
	exit(0);
    }

    /* this is the forwarder process */

    /* Cleanup all unneeded memory */
    PSID_clearMem(false); // @todo check how to allow true here

    /* save errno in case of error */
    if (pid == -1) eno = errno;

    /* prepare connection to child */
    if (close(controlfds[1]) < 0) {
	PSID_fwarn(errno, "close(%d)", controlfds[1]);
    }

    /* close the slave ttys / sockets */
    if (task->interactive) {
	close(stderrfds[1]);
	task->stdin_fd = stderrfds[0];
	task->stdout_fd = stderrfds[0];
	task->stderr_fd = stderrfds[0]; /* req. by SPAWNFAILED extension */
    } else {
	close(stdinfds[0]);
	task->stdin_fd = stdinfds[1];
	close(stdoutfds[1]);
	task->stdout_fd = stdoutfds[0];
	close(stderrfds[1]);
	task->stderr_fd = stderrfds[0];
    }

    /* set close-on-exec for fds */
    PSCio_setFDCloExec(task->stdin_fd, true);
    PSCio_setFDCloExec(task->stdout_fd, true);
    PSCio_setFDCloExec(task->stderr_fd, true);

    /* check if fork() was successful */
    if (pid == -1) {
	PSID_fwarn(eno, "fork()");
	goto error;
    }

    /* switch effective user */
    char *name = NULL;
    if (PSIDnodes_supplGrps(PSC_getMyID())) name = PSC_userFromUID(task->uid);

    if (!PSC_switchEffectiveUser(name, task->uid, task->gid)) {
	eno = EPERM;
	PSID_fwarn(eno, "switchEffectiveUser()");
	free(name);
	goto error;
    }
    free(name);

    /* check for a sign from the client */
    PSID_fdbg(PSID_LOG_SPAWN, "waiting for my child (%d)\n", pid);

    /* Pass the client's PID to the forwarder. */
    task->tid = PSC_getTID(-1, pid);

    /* Just wait a finite time for the client process */
    envStr = getenv("__PSI_ALARM_TMOUT");
    if (envStr) {
	int tmout;
	if (sscanf(envStr, "%d", &tmout) > 0) timeout.tv_sec = tmout;
    }
    timeout.tv_sec += 2;  /* 2 secs more than client */

    gettimeofday(&start, NULL);                   /* get starttime */
    timeradd(&start, &timeout, &end);             /* add given timeout */

    do {
	fd_set rfds;

	FD_ZERO(&rfds);
	FD_SET(controlfds[0], &rfds);

	gettimeofday(&start, NULL);               /* get NEW starttime */
	timersub(&end, &start, &stv);
	if (stv.tv_sec < 0) timerclear(&stv);

	int ret = select(controlfds[0] + 1, &rfds, NULL, NULL, &stv);
	if (ret == -1) {
	    if (errno == EINTR) {
		/* Interrupted syscall, just start again */
		const struct timeval delta = { .tv_sec = 0, .tv_usec = 10 };
		timersub(&end, &delta, &start);       /* assure next round */
		continue;
	    } else {
		eno = errno;
		PSID_fwarn(eno, "select() failed");
		break;
	    }
	} else if (!ret) {
	    PSID_flog("select(%d) timed out\n", controlfds[0]);
	    eno = ETIME;
	    statPID(pid);
	    break;
	} else {
	    break;
	}
	gettimeofday(&start, NULL);  /* get NEW starttime */
    } while (timercmp(&start, &end, <));

    if (!eno) {
	ssize_t ret = PSCio_recvBuf(controlfds[0], &eno, sizeof(eno));
	if (ret < 0) {
	    eno = errno;
	    PSID_fwarn(eno, "PSCio_recvBuf()");
	} else if (!ret) {
	    PSID_flog("ret is %zd\n", ret);
	    eno = EBADMSG;
	}
    }

error:
    /* Release the waiting daemon and exec forwarder */
    PSID_forwarder(task, controlfds[0], eno);

    /* never reached */
    exit(0);
}

/**
 * @brief Send accounting info on start of child
 *
 * Send info on the child currently started to the accounter. The
 * child is described within @a task.
 *
 * Actually a single messages of type @a PSP_ACCOUNT_CHILD is created.
 *
 * @param task Task structure holding information to send.
 *
 * @return No return value.
 */
static void sendAcctChild(PStask_t *task)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_ACCOUNT,
	    .dest =  PSC_getMyTID(),
	    .sender = task->tid,
	    .len = 0 },
	.type = PSP_ACCOUNT_CHILD };

    /* partition holder identifies job uniquely (logger's TID as fallback) */
    PStask_ID_t acctRoot = task->loggertid;
    if (task->partHolder != -1) acctRoot = task->partHolder;
    PSP_putTypedMsgBuf(&msg, "acctRoot", &acctRoot, sizeof(acctRoot));
    PSP_putTypedMsgBuf(&msg, "rank", &task->jobRank, sizeof(task->jobRank));
    PSP_putTypedMsgBuf(&msg, "uid", &task->uid, sizeof(task->uid));
    PSP_putTypedMsgBuf(&msg, "gid", &task->gid, sizeof(task->gid));

#define MAXARGV0 128
    /* job's name */
    char *cmd = strvGet(task->argV, 0);
    if (cmd) {
	size_t len = strlen(cmd), offset=0;
	if (len > MAXARGV0) {
	    char dots[]="...";
	    PSP_putTypedMsgBuf(&msg, "dots", dots, strlen(dots));
	    offset = len-MAXARGV0+3;
	}
	PSP_putTypedMsgBuf(&msg, "name", cmd + offset, strlen(cmd + offset));
    }
    PSP_putTypedMsgBuf(&msg, "trailing \\0", NULL, 0);

    sendMsg((DDMsg_t *)&msg);
}

/**
 * @brief Build sandbox and spawn process within
 *
 * Build a new sandbox and use @a creator to setup the actual process
 * described by @a task. This function is expected to take all
 * necessary measure to detach from the daemon and to actually create
 * the process. This might include further calls of fork().
 *
 * The new sandbox is connected to the local daemon via a UNIX stream
 * socketpair. One end of the socketpair will be passed to @a creator,
 * the other end is registered within @a task. All other file
 * descriptors (besides stdin/stdout/stderr) within the new sandbox
 * will be closed.
 *
 * All information only to be determined during start up of the
 * process are stored upon return within the task structures. This
 * includes the task ID and the file descriptor connecting the local
 * daemon to the process.
 *
 * @param creator Actual mechanism to create the process
 *
 * @param task Task structure describing the process to create
 *
 * @return On success, 0 is returned. If something went wrong, a value
 * different from 0 is returned. This value might be interpreted as an
 * errno describing the problem that occurred during the spawn.
 */
static int buildSandboxAndStart(PSIDspawn_creator_t *creator, PStask_t *task)
{
    int socketfds[2];     /* sockets for communication with forwarder */

    if (!creator) {
	PSID_fwarn(EINVAL, "no creator");
	return EINVAL;
    }

    if (PSID_getDebugMask() & PSID_LOG_SPAWN) {
	char tasktxt[4096];
	PStask_snprintf(tasktxt, sizeof(tasktxt), task);
	PSID_flog("task %p: %s\n", task, tasktxt);
    }

    /* create a socketpair for communication between daemon and forwarder */
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, socketfds) < 0) {
	int eno = errno;
	PSID_fwarn(eno, "socketpair()");
	return eno;
    }

    bool blocked = PSID_blockSig(SIGTERM, true);
    /* fork the forwarder */
    pid_t pid = fork();
    /* save errno in case of error */
    int eno = errno;

    if (!pid) {
	/* this is the forwarder process */

	PSID_resetSigs();
	PSID_blockSig(SIGTERM, false);
	/* keep SIGCHLD blocked */

	/*
	 * Create a new process group. This is needed since the daemon
	 * kills whole process groups. Otherwise the daemon might
	 * commit suicide by sending signals to its clients.
	 */
	setpgid(0, 0);

	/* close all fds except the control channel, stdin/stdout/stderr and
	   the connecting socket */
	/* Start with connection to syslog */
	closelog();
	/* Then all the rest */
	int maxFD = sysconf(_SC_OPEN_MAX);
	for (int fd = STDERR_FILENO + 1; fd < maxFD; fd++) {
	    if (fd != socketfds[1]) close(fd);
	}
	/* Reopen the syslog and rename the tag */
	openlog("psidforwarder", LOG_PID|LOG_CONS, PSID_config->logDest);

	/* Get rid of now useless selectors */
	Selector_init(NULL);
	/* Get rid of obsolete timers */
	Timer_init(NULL);

	PSC_setDaemonFlag(false);

	task->fd = socketfds[1];

	creator(task);
    }

    /* this is the parent process */
    PSID_blockSig(SIGTERM, blocked);

    /* close forwarders end of the socketpair */
    close(socketfds[1]);

    /* check if fork() was successful */
    if (pid == -1) {
	close(socketfds[0]);

	PSID_fwarn(eno, "fork()");

	return eno;
    }

    task->tid = PSC_getTID(-1, pid);
    task->fd = socketfds[0];
    /* check for a sign of the forwarder */
    PSID_fdbg(PSID_LOG_SPAWN, "waiting for my child (%d)\n", pid);

    return 0;
}

/**
 * @brief Send accounting info on start of job
 *
 * Send info on the jobs currently started to the accounter. The job
 * is described within @a task, all messages are sent as @a sender.
 *
 * Actually two types of messages are created. First of all a @a
 * PSP_ACCOUNT_START message is sent. This is followed by one or more
 * @a PSP_ACCOUNT_SLOTS containing chunks of slots describing the
 * allocated partition.
 *
 * @param sender Task identity to send as.
 *
 * @param task Task structure holding information to send.
 *
 * @return No return value.
 */
static void sendAcctStart(PStask_ID_t sender, PStask_t *task)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_ACCOUNT,
	    .dest =  PSC_getMyTID(),
	    .sender = sender,
	    .len = 0 },
	.type = PSP_ACCOUNT_START };

    /* partition holder identifies job uniquely (logger's TID as fallback) */
    PStask_ID_t acctRoot = task->loggertid;
    if (task->partHolder != -1) acctRoot = task->partHolder;
    PSP_putTypedMsgBuf(&msg, "acctRoot", &acctRoot, sizeof(acctRoot));
    PSP_putTypedMsgBuf(&msg, "rank", &task->rank, sizeof(task->rank));
    PSP_putTypedMsgBuf(&msg, "uid", &task->uid, sizeof(task->uid));
    PSP_putTypedMsgBuf(&msg, "gid", &task->gid, sizeof(task->gid));
    int32_t num = task->totalThreads;
    PSP_putTypedMsgBuf(&msg, "num", &num, sizeof(num));

    sendMsg((DDMsg_t *)&msg);

    unsigned short maxCPUs = 0;
    int pSize = task->partitionSize;
    for (int32_t slot = 0; slot < pSize; slot++) {
	unsigned short cpus = PSIDnodes_getNumThrds(task->partition[slot].node);
	if (cpus > maxCPUs) maxCPUs = cpus;
    }
    if (!pSize || !maxCPUs) {
	PSID_flog("no CPUs\n");
	return;
    }

    msg.type = PSP_ACCOUNT_SLOTS;

    uint16_t nBytes = PSCPU_bytesForCPUs(maxCPUs);
    int32_t slotsChunk = 1024 / (sizeof(PSnodes_ID_t) + nBytes);

    for (int32_t slot = 0; slot < pSize; slot++) {
	if (! (slot%slotsChunk)) {
	    uint16_t chunk =
		(pSize-slot < slotsChunk) ? pSize-slot : slotsChunk;

	    if (slot) sendMsg((DDMsg_t *)&msg);

	    msg.header.len = 0;

	    PSP_putTypedMsgBuf(&msg, "loggertid", &task->loggertid,
			       sizeof(task->loggertid));
	    PSP_putTypedMsgBuf(&msg, "uid", &task->uid, sizeof(task->uid));
	    PSP_putTypedMsgBuf(&msg, "chunk", &chunk, sizeof(chunk));
	    PSP_putTypedMsgBuf(&msg, "nBytes", &nBytes, sizeof(nBytes));
	}

	PSP_putTypedMsgBuf(&msg, "node", &task->partition[slot].node,
			   sizeof(task->partition[slot].node));

	PSCPU_set_t setBuf;
	PSCPU_extract(setBuf, task->partition[slot].CPUset, nBytes);
	PSP_putTypedMsgBuf(&msg, "CPUset", setBuf, nBytes);
    }

    sendMsg((DDMsg_t *)&msg);
}


/**
 * @brief Check spawn request
 *
 * Check if the request to spawn as decoded in @a task is correct,
 * i.e. if it does not violate some security measures or node
 * configurations.
 *
 * While checking the request it is assumed that its origin is the
 * task @a sender.
 *
 * If the request is determined to be valid, a series of accounting
 * messages is created via calling @ref sendAcctStart().
 *
 * @param sender Assumed origin of the spawn request.
 *
 * @param task Task structure describing the spawn request.
 *
 * @return On success, 0 is returned. Otherwise the return value might
 * be interpreted as an errno.
 */
static int checkRequest(PStask_ID_t sender, PStask_t *task)
{
    PStask_t *ptask, *stask;

    stask = PStasklist_find(&managedTasks, sender);
    if (!stask) {
	PSID_flog("sending task %s not found\n", PSC_printTID(sender));
	return EACCES;
    }

    if (sender != task->ptid && stask->group != TG_FORWARDER) {
	/* Sender has to be parent or a trusted forwarder */
	PSID_flog("spawner %s tries to cheat\n", PSC_printTID(sender));
	return EACCES;
    }

    ptask = PStasklist_find(&managedTasks, task->ptid);
    if (!ptask) {
	/* Parent not found */
	PSID_flog("parent task %s not found\n", PSC_printTID(task->ptid));
	return EACCES;
    }

    if (ptask->uid && task->uid != ptask->uid) {
	/* Spawn tries to change uid */
	PSID_flog("try to setuid() task->uid %d  ptask->uid %d\n",
		  task->uid, ptask->uid);
	return EACCES;
    }

    if (ptask->gid && task->gid != ptask->gid) {
	/* Spawn tries to change gid */
	PSID_flog("try to setgid() task->gid %d  ptask->gid %d\n",
		  task->gid, ptask->gid);
	return EACCES;
    }

    if (!PSIDnodes_isStarter(PSC_getMyID()) && task->group != TG_ADMINTASK
	&& ptask->group == TG_LOGGER) {
	/* starting not allowed */
	PSID_flog("spawning not allowed\n");
	return EACCES;
    }

    if (task->group == TG_ADMINTASK
	&& !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMUSER,
			       (PSIDnodes_guid_t){.u=ptask->uid})
	&& !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMGROUP,
			       (PSIDnodes_guid_t){.g=ptask->gid})) {
	/* no permission to start admin task */
	PSID_flog("no permission to spawn admintask\n");
	return EACCES;
    }

    if ((task->group == TG_SERVICE || task->group == TG_SERVICE_SIG
	|| task->group == TG_KVS) && task->rank >= -1) {
	/* wrong rank for service task */
	PSID_flog("rank %d for service task\n", task->rank);
	return EINVAL;
    }

    PSID_fdbg(PSID_LOG_SPAWN, "request from %s ok\n", PSC_printTID(task->ptid));

    /* Accounting info */
    if (ptask->group == TG_LOGGER && ptask->partitionSize > 0)
	sendAcctStart(sender, ptask);

    return 0;
}

/**
 * @brief Spawn new task.
 *
 * Build a new sandbox and spawn the process described by @a client
 * within. In order to do this, first of all a forwarder is created
 * that sets up a sandbox for the client process to run in. The
 * actual client process is started within this sandbox.
 *
 * All necessary information determined during start up of the
 * forwarder and client process is stored within the corresponding
 * task structures. For the forwarder this includes the task ID and
 * the file descriptor connecting the local daemon to the
 * forwarder. For the client only the task ID is stored.
 *
 * @param task Task structure describing the task to create.
 *
 * @return On success, 0 is returned. If something went wrong, a value
 * different from 0 is returned. This value might be interpreted as an
 * errno describing the problem that occurred during the spawn.
 */
static int spawnTask(PStask_t *task)
{
    if (!task) return EINVAL;

    /* Call hook once per task. */
    if (PSIDhook_call(PSIDHOOK_SPAWN_TASK, task) < 0) {
	PSID_flog("PSIDHOOK_SPAWN_TASK failed\n");
	return EINVAL;   // most probably some illegal value was passed
    }

    /* add some last minute extra environment */
    char tmp[32];
    sprintf(tmp, "%ld", task->loggertid);
    envSet(task->env, "PS_SESSION_ID", tmp);

    sprintf(tmp, "%ld", task->spawnertid);
    envSet(task->env, "PS_JOB_ID", tmp);

    sprintf(tmp, "%d", task->resID);
    envSet(task->env, "PS_RESERVATION_ID", tmp);

    sprintf(tmp, "%d", task->jobRank);
    envSet(task->env, "PS_JOB_RANK", tmp);

    /* now try to start the task */
    int err = buildSandboxAndStart(execForwarder, task);

    if (!err) {
	/* prepare forwarder task */
	task->childGroup = task->group;
	task->group = TG_FORWARDER;
	task->protocolVersion = PSProtocolVersion;
	/* Enqueue the forwarder */
	PStasklist_enqueue(&managedTasks, task);
	/* The forwarder is already connected and established */
	PSIDclient_register(task->fd, task->tid, task);
	PSIDclient_setEstablished(task->fd, NULL, NULL);
	/* Tell everybody about the new forwarder task */
	incTaskCount(false);
    } else {
	PSID_fdwarn(PSID_LOG_SPAWN, err, "buildSandboxAndStart()");
    }

    return err;
}

#define NUM_CPUSETS 8

/** Structure to store information on pending PSP_DD_CHILDRESREL messages */
typedef struct {
    list_t next;         /**< used to put into reservation-lists */
    PStask_ID_t dest;    /**< destination task ID of this pending message */
    PSrsrvtn_ID_t resID; /**< reservation ID of this pending release message */
    PStask_ID_t sender;  /**< sender task ID to send this pending message */
    int16_t pendSlots;   /**< number of slots missing until (full) send */
    uint16_t numSlots;   /**< number of slots merged into sets */
    PSCPU_set_t sets[NUM_CPUSETS]; /**< accumulated HW-threads to release */
} PendCRR_t;

/** List of pending PSP_DD_CHILDRESREL messages */
static LIST_HEAD(pendCRRList);

static PendCRR_t *newPendCRR(void)
{
    PendCRR_t *crr = malloc(sizeof(*crr));
    if (crr) {
	for (uint16_t s = 0; s < NUM_CPUSETS; s++) PSCPU_clrAll(crr->sets[s]);
	crr->numSlots = 0;
    }
    return crr;
}

static void delPendCRR(PendCRR_t *crr)
{
    free(crr);
}

static PendCRR_t *findPendCRR(PStask_ID_t dest, PSrsrvtn_ID_t resID)
{
    list_t *c;
    list_for_each(c, &pendCRRList) {
	PendCRR_t * crr = list_entry(c, PendCRR_t, next);
	if (crr->dest == dest && crr->resID == resID) return crr;
    }
    return NULL;
}

static void sendPendCRR(PendCRR_t *crr)
{
    if (!crr) return;

    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_CHILDRESREL,
	    .dest = crr->dest,
	    .sender = crr->sender,
	    .len = 0 } };
    uint16_t nBytes = PSCPU_bytesForCPUs(PSIDnodes_getNumThrds(PSC_getMyID()));

    PSP_putMsgBuf(&msg, "nBytes", &nBytes, sizeof(nBytes));
    PSP_putMsgBuf(&msg, "CPUset", crr->sets[0], nBytes);
    PSP_putMsgBuf(&msg, "numSlots", &crr->numSlots, sizeof(crr->numSlots));
    PSP_putMsgBuf(&msg, "resID", &crr->resID, sizeof(crr->resID));

    int nCPUs = PSCPU_getCPUs(crr->sets[0], NULL, nBytes * 8);
    uint16_t s = 1;
    for (; s < NUM_CPUSETS && PSCPU_any(crr->sets[s], nBytes * 8); s++) {
	PSP_putMsgBuf(&msg, "CPUset", crr->sets[s], nBytes);
	nCPUs += PSCPU_getCPUs(crr->sets[s], NULL, nBytes * 8);
    }

    PSID_fdbg(PSID_LOG_PART, "%d CPUs in %d slots of res %#x in %d sets to %s",
	      nCPUs, crr->numSlots, crr->resID, s, PSC_printTID(crr->dest));
    PSID_dbg(PSID_LOG_PART, " from %s (%d pending)\n",
	     PSC_printTID(crr->sender), crr->pendSlots);
    if (sendMsg(&msg) < 0) {
	PSID_fwarn(errno, "sendMsg(%s)", PSC_printTID(crr->dest));
    }
}

static void sendAllPendCRR(void)
{
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &pendCRRList) {
	PendCRR_t * crr = list_entry(c, PendCRR_t, next);
	sendPendCRR(crr);
	list_del(&crr->next);
	delPendCRR(crr);
    }
}

static int clearMemPendCRR(void *info)
{
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &pendCRRList) {
	PendCRR_t * crr = list_entry(c, PendCRR_t, next);
	list_del(&crr->next);
	delPendCRR(crr);
    }

    return 0;
}

/**
 * @brief Send a PSP_DD_CHILDRESREL message
 *
 * Send a message of type PSP_DD_CHILDRESREL to a partition
 * holder. The partition is determined from the reservation that is
 * used by the task @a task. @a task also determines the HW-threads to
 * be reported to the partition holder.
 *
 * This will release the now unused resources and enable them to be
 * reused. The task ID @a sender will act as the messenger reporting
 * the released resources.
 *
 * Unless the flag @a combine is set, this message will be sent
 * immediately. If @a combine is set, it is tried to merge subsequent
 * calls to this function to reduce the total number of messages sent
 * to the partition holder and to limit the effort on the receiving
 * side.
 *
 * The strategy applied is as follows:
 *
 * - Identify a pending messages to the partition holder concerning
 *   the reservation used by @a task and merge the new request. Send
 *   the message if no further requests are expected for this
 *   combination
 *
 * - If no pending message was found, identify the maximum number of
 *   expected requests for the combination of partition holder and the
 *   reservation used by @a task (i.e. number of tasks in managedTasks
 *   for this combination) and created a pending message if necessary;
 *   otherwise send the message immediately
 *
 * - From time to time (i.e. in the loop-action) send all pending
 *   messages via @ref sendAllPendCRR()
 *
 * @param task Task describing resources to report, destination, etc.
 *
 * @param sender Messenger reporting about the resources to be released
 *
 * @param combine Flag merging subsequent calls into this
 *
 * @return No return value
 */
static void sendCHILDRESREL(PStask_t *task, PStask_ID_t sender, bool combine)
{
    if (!task || task->group == TG_SERVICE || task->group == TG_SERVICE_SIG
	|| task->group == TG_ADMINTASK || task->group == TG_KVS
	|| task->group == TG_PLUGINFW) {
	PSID_fdbg(PSID_LOG_PART, "nothing to send for task %s from %s\n",
		  task ? PStask_printGrp(task->group) : "(none)",
		  PSC_printTID(sender));
	return;
    }

    uint16_t nBytes = PSCPU_bytesForCPUs(PSIDnodes_getNumThrds(PSC_getMyID()));
    if (!PSCPU_any(task->CPUset, nBytes * 8)) {
	PSID_fdbg(PSID_LOG_PART, "no HW threads to send in task %s",
		  PSC_printTID(task->tid));
	PSID_dbg(PSID_LOG_PART, " from %s\n", PSC_printTID(sender));
	return;
    }

    PStask_ID_t dest = task->loggertid;
    PSresinfo_t *res = PSID_findResInfo(task->loggertid, task->spawnertid,
					task->resID);
    if (!res) {
	PSID_fdbg(PSID_LOG_PART, "no reservation %#x (job %s", task->resID,
		  PSC_printTID(task->spawnertid));
	PSID_dbg(PSID_LOG_PART, " session %s)", PSC_printTID(task->loggertid));
	PSID_dbg(PSID_LOG_PART, " for task %s\n", PSC_printTID(task->tid));
    }
    if (res && res->partHolder) dest = res->partHolder;


    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_CHILDRESREL,
	    .dest = dest,
	    .sender = sender,
	    .len = 0 } };

    if (combine) {
	PendCRR_t *crr = findPendCRR(dest, task->resID);
	if (!crr) {
	    crr = newPendCRR();
	    if (!crr) {
		PSID_fwarn(ENOMEM, "newPendCRR(%s, %#x)",
			   PSC_printTID(dest), task->resID);
		return;
	    }

	    crr->dest = dest;
	    crr->resID = task->resID;
	    crr->sender = sender;
	    /* determine total number of calls expected */
	    crr->pendSlots = 1; // this call
	    list_t *t;
	    list_for_each(t, &managedTasks) {
		PStask_t *thisT = list_entry(t, PStask_t, next);
		if (thisT->deleted || thisT->group == TG_FORWARDER) continue;
		if (thisT->loggertid != task->loggertid
		    || thisT->resID != task->resID) continue;
		if (!PSCPU_any(thisT->CPUset, nBytes * 8)) continue;
		if (thisT->tid == task->tid) continue;
		crr->pendSlots++;
	    }
	    PSID_fdbg(PSID_LOG_PART, "%s misses %d\n", PSC_printTID(task->tid),
		      crr->pendSlots);
	    list_add_tail(&crr->next, &pendCRRList);
	}

	/* fit this call into crr */
	uint16_t s = 0;
	while (s < NUM_CPUSETS
	       && PSCPU_overlap(task->CPUset, crr->sets[s], 8 * nBytes)) s++;
	if (s == NUM_CPUSETS) {
	    /* no space for this call => send now and reset */
	    PSID_fdbg(PSID_LOG_PART, "%s no sets\n", PSC_printTID(task->tid));
	    sendPendCRR(crr);
	    /* reset crr */
	    for (s = 0; s < NUM_CPUSETS; s++) PSCPU_clrAll(crr->sets[s]);
	    crr->numSlots = 0;
	    s = 0;
	}
	PSCPU_addCPUs(crr->sets[s], task->CPUset);
	crr->numSlots++;
	crr->pendSlots--;
	if (!crr->pendSlots) {
	    /* all slots included => lets send */
	    PSID_dbg(PSID_LOG_PART, "%s filled\n", PSC_printTID(task->tid));
	    sendPendCRR(crr);
	    list_del(&crr->next);
	    delPendCRR(crr);
	}
	return;
    } else {
	/* send message immediately */
	PSP_putMsgBuf(&msg, "nBytes", &nBytes, sizeof(nBytes));
	PSP_putMsgBuf(&msg, "CPUset", task->CPUset, nBytes);
	uint16_t one = 1;
	PSP_putMsgBuf(&msg, "numSlots", &one, sizeof(one));
	PSP_putMsgBuf(&msg, "resID", &task->resID, sizeof(task->resID));
    }
    PSID_fdbg(PSID_LOG_PART, "PSP_DD_CHILDRESREL to %s with CPUs %s of",
	      PSC_printTID(dest), PSCPU_print_part(task->CPUset, nBytes));
    PSID_dbg(PSID_LOG_PART, " res %#x from %s\n", task->resID,
	     PSC_printTID(sender));

    if (sendMsg(&msg) < 0) PSID_fwarn(errno, "sendMsg(%s)", PSC_printTID(dest));
}

static inline bool isServiceTask(PStask_group_t group)
{
    return (group == TG_SERVICE || group == TG_SERVICE_SIG
	    || group == TG_ADMINTASK || group == TG_KVS);
}

int PSIDspawn_fillTaskFromResInfo(PStask_t *task, PSresinfo_t *res)
{
    /* we depend on PSP_DD_RESCREATED and PSP_DD_RESSLOTS message */
    if (!res || !res->nLocalSlots) {
	/* Resinfo not yet complete => delay task */
	task->delayReasons |= DELAY_RESINFO;
	return 0;
    }

    task->jobRank = task->rank - res->rankOffset;
    if (task->jobRank < res->minRank || task->jobRank > res->maxRank) {
	PSID_flog("res %#x rank %d out of range\n", res->resID, task->rank);
	return EADDRNOTAVAIL;
    }

    if (!res->localSlots) {
	PSID_flog("no local slots in res %#x\n", res->resID);
	return EADDRNOTAVAIL;
    }

    /* try to fill the CPUset */
    for (uint16_t s = 0; s < res->nLocalSlots; s++) {
	if (task->jobRank != res->localSlots[s].rank) continue;

	/* local slot found for rank */
	memcpy(task->CPUset, res->localSlots[s].CPUset, sizeof(task->CPUset));
	task->partHolder = res->partHolder;

	if (!PSCPU_any(task->CPUset, PSCPU_MAX)) {
	    PSID_flog("res %#x rank %d exhausted\n", res->resID, task->jobRank);
	    return EADDRINUSE;
	}

	PSID_fdbg(PSID_LOG_SPAWN, "res %#x rank %d got cores: ...%s from %s\n",
		  res->resID, task->jobRank, PSCPU_print_part(task->CPUset, 8),
		  PSC_printTID(task->partHolder));
	return 0;
    }

    /* we missed the resource for the requested rank ?! */
    PSID_flog("res %#x rank %d (global %d) not found\n", res->resID,
	      task->jobRank, task->rank);
    return EADDRNOTAVAIL;
}

/**
 * @brief Spawn processes
 *
 * Actually spawn processes as described on the data buffer @a
 * rData. It contains a whole message of type PSP_CD_SPAWNREQUEST
 * holding all information on the processes requested to be
 * created. Additional information can be obtained for @a msg
 * containing meta-information of the last fragment received.
 *
 * @param msg Message header (including the type) of the last fragment
 *
 * @param rData Data buffer presenting the actual PSP_CD_SPAWNREQUEST
 *
 * @return No return value
 */
static void handleSpawnReq(DDTypedBufferMsg_t *msg, PS_DataBuffer_t rData)
{
    DDErrorMsg_t answer = {
	.header = {
	    .type = PSP_CD_SPAWNFAILED,
	    .sender = msg->header.dest,
	    .dest = msg->header.sender,
	    .len = sizeof(answer) },
	.error = 0,
	.request = 0,};

    /* ensure we use the same byteorder as libpsi */
    bool byteOrder = setByteOrder(true);

    /* fetch info from message */
    uint32_t num;
    getUint32(rData, &num);

    /* task describing the client(s) to spawn */
    PStask_t *task = PStask_new();
    /* standard task blob */
    size_t len;
    void *blob = getDataM(rData, &len);
    bool decodeRes = PStask_decodeTask(blob, len, task);
    free(blob);
    if (!decodeRes) {
	PStask_delete(task);
	PSID_flog("unable to decode task from %s\n",
		  PSC_printTID(msg->header.sender));
	/* cleanup psserial's byteorder */
	setByteOrder(byteOrder);

	/* send at least a single answer */
	answer.error = EBADMSG;
	answer.request = 0; // we don't know the rank!
	sendMsg(&answer);

	return;
    }

    task->spawnertid = msg->header.sender;
    task->workingdir = getStringM(rData);
    getArgV(rData, task->argV);
    getEnv(rData, task->env);

    serial_Err_Types_t errState = PSdbGetErrState(rData);
    if (errState != E_PSSERIAL_SUCCESS) {
	PSID_flog("unpacking failed: %s\n", serialStrErr(errState));
	answer.error = EBADMSG;
	/* send one answer per rank */
	for (uint32_t r = 0; r < num; r++) {
	    answer.request = task->rank + r;
	    sendMsg(&answer);
	}
	PStask_delete(task);
	/* cleanup psserial's byteorder */
	setByteOrder(byteOrder);
	return;
    }

    /* Call hook once per PSP_CD_SPAWNREQUEST meaning once per node.
     * Pay attention that the task provided is only a prototype, containing
     * all information shared between all tasks of the spawn but not containing
     * the task specific stuff like rank specific environment. */
    if (PSIDhook_call(PSIDHOOK_RECV_SPAWNREQ, task) < 0) {
	PSID_flog("PSIDHOOK_RECV_SPAWNREQ failed\n");
	answer.error = EINVAL; // most probably some illegal value was passed
	/* send one answer per rank */
	for (uint32_t r = 0; r < num; r++) {
	    answer.request = task->rank + r;
	    sendMsg(&answer);
	}
	PStask_delete(task);
	/* cleanup psserial's byteorder */
	setByteOrder(byteOrder);
	return;
    }

    /* service tasks will not be pinned (i.e. pinned to all HW threads) */
    if (isServiceTask(task->group)) PSCPU_setAll(task->CPUset);

    PSresinfo_t *resI = PSID_findResInfo(task->loggertid,
					 task->spawnertid, task->resID);
    for (uint32_t r = 0; r < num; r++) {
	PStask_t *clone = PStask_clone(task);

	answer.request = task->rank + r;
	if (!clone) {
	    answer.error = ENOMEM;
	    sendMsg(&answer);
	    continue;
	}
	clone->rank = answer.request;

	env_t extraEnv;
	getEnv(rData, extraEnv);
	envAppend(clone->env, extraEnv, NULL);
	envDestroy(extraEnv);

	if (!isServiceTask(task->group)) {
	    answer.error = PSIDspawn_fillTaskFromResInfo(clone, resI);
	    if  (answer.error) {
		sendMsg(&answer);
		PStask_delete(clone);
		continue;
	    }
	}

	/* Now clone might be used for actual spawn */
	if (PSID_getDebugMask() & PSID_LOG_SPAWN) {
	    char tasktxt[4096];
	    PStask_snprintf(tasktxt, sizeof(tasktxt), clone);
	    PSID_flog("spawning %p: %s\n", clone, tasktxt);
	}

	if (clone->delayReasons) {
	    /* PSIDHOOK_RECV_SPAWNREQ may delay spawning */
	    PSIDspawn_delayTask(clone);
	} else {
	    answer.error = spawnTask(clone);
	}

	if (answer.error) {
	    /* send only on failure. success reported by forwarder */
	    sendMsg(&answer);
	    sendCHILDRESREL(clone, PSC_getMyTID(), false);

	    PStask_delete(clone);
	}
    }

    /* reset psserial's byteorder */
    setByteOrder(byteOrder);

    PStask_delete(task);

    return;
}

/**
 * @brief Handle a PSP_CD_SPAWNREQUEST message
 *
 * Handle the message @a msg of type PSP_CD_SPAWNREQUEST.
 *
 * This will spawn processes as described within this message. Since
 * the serialization layer is utilized depending on the number of
 * processes to spawn and the sizes of the argument vector and the
 * environment the messages might be split into multiple fragments.
 *
 * This function will collect these fragments into a single message
 * using the serialization layer or forward single fragments to the
 * final destination of the spawn request. Furthermore, on the node of
 * the initiating task, various tests are undertaken in order to
 * determine if the spawn is allowed. If all tests pass, the message
 * is forwarded to the target-node where the processes to spawn are
 * created.
 *
 * The actual handling of the spawn request once all fragments are
 * received is done within @ref handleSpawnReq().
 *
 * @param msg Pointer to message holding the fragment to handle
 *
 * @return Always return true
 */
static bool msg_SPAWNREQUEST(DDTypedBufferMsg_t *msg)
{
    PStask_t *ptask = NULL;
    DDErrorMsg_t answer = {
	.header = {
	    .type = PSP_CD_SPAWNFAILED,
	    .sender = msg->header.dest,
	    .dest = msg->header.sender,
	    .len = sizeof(answer) },
	.error = 0,
	.request = 0,};
    bool localSender = (PSC_getID(msg->header.sender) == PSC_getMyID());
    uint32_t num;
    int32_t rank = -1;
    PStask_group_t group = TG_ANY;

    size_t used = 0;
    uint16_t fragNum;
    fetchFragHeader(msg, &used, NULL, &fragNum, NULL, NULL);

    PSID_fdbg(PSID_LOG_SPAWN, "fragment %d from %s msglen %d\n", fragNum,
	      PSC_printTID(msg->header.sender), msg->header.len);

    /* First fragment, take a peek if it is from my node */
    if (localSender && fragNum == 0) {
	PS_DataBuffer_t data =
	    PSdbNew(msg->buf + used,
		    msg->header.len - DDTypedBufMsgOffset - used);

	/* ensure we use the same byteorder as libpsi */
	bool byteOrder = setByteOrder(true);

	/* fetch info from message */
	getUint32(data, &num);
	PStask_t *task = PStask_new();

	/* standard task blob */
	size_t len;
	void *blob = getDataM(data, &len);
	bool decodeRes = PStask_decodeTask(blob, len, task);
	free(blob);
	PSdbDelete(data);

	/* reset psserial's byteorder */
	setByteOrder(byteOrder);

	if (!decodeRes) {
	    PStask_delete(task);
	    PSID_flog("broken task from %s?!\n", PSC_printTID(msg->header.sender));
	    /* send at least a single answer */
	    answer.error = EBADMSG;
	    answer.request = 0; // we don't know the rank!
	    sendMsg(&answer);

	    return true;
	}

	/* Check if everything is okay */
	answer.error = checkRequest(msg->header.sender, task);

	/* Store some info from task for later use */
	group = task->group;
	rank = task->rank;

	PStask_delete(task);

	if (answer.error) {
	    for (uint32_t r = 0; r < num; r++) {
		answer.request = rank + r;
		sendMsg(&answer);
	    }

	    return true;
	}

	/* Since checkRequest() did not fail, we will find ptask */
	ptask = PStasklist_find(&managedTasks, msg->header.sender);
	if (!ptask) {
	    PSID_flog("no parent task?!\n");
	    return true;
	}

	/* keep track of resources not yet utilized to spawn tasks */
	if (!isServiceTask(group) && ptask->spawnSlots) {
	    if (rank + num - 1 >= ptask->spawnSlotsNum) {
		PSID_flog("ranks %d-%d out of range\n", rank, rank + num -1);
		num = ptask->spawnSlotsNum - rank;
	    }
	    for (uint32_t r = 0; r < num; r++) {
		/* Invalidate this entry */
		PSCPU_clrAll(ptask->spawnSlots[rank+r].CPUset);
	    }
	}
    }

    if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
	/* destination is here */
	recvFragMsg(msg, handleSpawnReq);
	return true;
    }

    /* destination is remote */

    if (!PSIDnodes_isUp(PSC_getID(msg->header.dest))) {
	answer.error = EHOSTDOWN;
	if (localSender && fragNum == 0) {
	    /* first fragment */
	    for (uint32_t r = 0; r < num; r++) {
		answer.request = rank + r;
		sendMsg(&answer);
	    }
	} /* ignore follow-up fragments */
	return true;
    }

    if (!localSender) {
	PSID_flog("won't relay %s", PSC_printTID(msg->header.sender));
	PSID_log("->%s\n", PSC_printTID(msg->header.dest));
	return false;
    }

    PSID_fdbg(PSID_LOG_SPAWN, "forward to node %d\n",
	      PSC_getID(msg->header.dest));
    if (sendMsg(msg) < 0 && fragNum == 0) {
	answer.error = errno;
	for (uint32_t r = 0; r < num; r++) {
	    answer.request = rank + r;
	    sendMsg(&answer);
	}
    }
    return true;
}

/**
 * @brief Drop a PSP_CD_SPAWNREQUEST message
 *
 * Drop the message @a msg of type PSP_CD_SPAWNREQUEST.
 *
 * Since the spawning process waits for a reaction to its request a
 * corresponding number of answers is created. Since the corresponding
 * information is only contained in the first fragment, all further
 * fragments are ignored.
 *
 * @param msg Pointer to message to drop
 *
 * @return Always return true
 */
static bool drop_SPAWNREQUEST(DDTypedBufferMsg_t *msg)
{
    DDErrorMsg_t errMsg = {
	.header = {
	    .type = PSP_CD_SPAWNFAILED,
	    .sender = msg->header.dest,
	    .dest = msg->header.sender,
	    .len = sizeof(errMsg) },
	.error = EHOSTDOWN,
	.request = 0,};

    PSID_fdbg(PSID_LOG_SPAWN, "from %s msglen %d\n",
	      PSC_printTID(msg->header.sender), msg->header.len);

    size_t used = 0;
    uint16_t fragNum;
    fetchFragHeader(msg, &used, NULL, &fragNum, NULL, NULL);

    /* Ignore trailing fragments */
    if (fragNum) return true;

    /* Extract num and rank from message to drop */
    PS_DataBuffer_t data = PSdbNew(msg->buf + used,
				   msg->header.len - DDTypedBufMsgOffset - used);

    /* ensure we use the same byteorder as libpsi */
    bool byteOrder = setByteOrder(true);

    /* fetch info from message */
    uint32_t num;
    getUint32(data, &num);
    PStask_t *task = PStask_new();

    /* standard task blob */
    size_t len;
    void *blob = getDataM(data, &len);
    bool decodeRes = PStask_decodeTask(blob, len, task);
    free(blob);
    PSdbDelete(data);

    /* reset psserial's byteorder */
    setByteOrder(byteOrder);

    int rank = task->rank;

    PStask_delete(task);

    if (!decodeRes) {
	PSID_flog("failed for msg from %s\n", PSC_printTID(msg->header.sender));
	return true;
    }

    for (uint32_t i = 0; i < num; i++) {
	errMsg.request = rank + i;
	sendMsg(&errMsg);
    }
    return true;
}

/**
 * List of tasks delayed to get spawned. They shall be started later
 * via @ref PSIDspawn_startDelayedTasks().
 */
static LIST_HEAD(delayedTasks);

void PSIDspawn_delayTask(PStask_t *task)
{
    PSID_fdbg(PSID_LOG_SPAWN, "ptid %s reason %#x \n", PSC_printTID(task->ptid),
	      task->delayReasons);
    PStasklist_dequeue(task);
    list_add_tail(&task->next, &delayedTasks);
}

void PSIDspawn_startDelayedTasks(PSIDspawn_filter_t filter, void *info)
{
    list_t *t, *tmp;
    list_for_each_safe(t, tmp, &delayedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);
	if (task->deleted) continue;
	if (filter && !filter(task, info)) continue;
	if (task->delayReasons) continue;

	DDErrorMsg_t answer = {
	    .header = {
		.type = PSP_CD_SPAWNFAILED,
		.sender = PSC_getMyTID(),
		.dest = task->tid,
		.len = sizeof(answer) },
	    .error = 0,
	    .request = task->rank};

	if (PSID_getDebugMask() & PSID_LOG_SPAWN) {
	    char tasktxt[4096];
	    PStask_snprintf(tasktxt, sizeof(tasktxt), task);
	    PSID_flog("spawning %p: %s\n", task, tasktxt);
	}

	PStasklist_dequeue(task);
	answer.error = spawnTask(task);

	if (answer.error) {
	    /* send only on failure. success reported by forwarder */
	    sendMsg(&answer);
	    sendCHILDRESREL(task, PSC_getMyTID(), false);

	    PStask_delete(task);
	}
    }
}

void PSIDspawn_cleanupDelayedTasks(PSIDspawn_filter_t filter, void *info)
{
    list_t *t;
    list_for_each(t, &delayedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);
	if (task->deleted) continue;
	if (filter && !filter(task, info)) continue;

	task->deleted = true;
    }
}

void PSIDspawn_cleanupByNode(PSnodes_ID_t node)
{
    PSID_fdbg(PSID_LOG_SPAWN, "node %d\n", node);

    list_t *t;
    list_for_each(t, &delayedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);
	if (PSC_getID(task->tid) == node) task->deleted = true;
    }
}

void PSIDspawn_cleanupBySpawner(PStask_ID_t tid)
{
    PSID_fdbg(PSID_LOG_SPAWN, "%s\n", PSC_printTID(tid));

    list_t *t;
    list_for_each(t, &delayedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);
	if (task->deleted) continue;
	if (task->tid != tid) continue;

	DDErrorMsg_t answer = {
	    .header = {
		.type = PSP_CD_SPAWNFAILED,
		.sender = PSC_getMyTID(),
		.dest = task->tid,
		.len = sizeof(answer) },
	    .error = ECHILD,
	    .request = task->rank};

	task->deleted = true;
	sendMsg(&answer);
	if (PSCPU_any(task->CPUset, PSCPU_MAX)) {
	    sendCHILDRESREL(task, PSC_getMyTID(), true);
	    PSCPU_clrAll(task->CPUset);
	}
    }
}

/**
 * @brief Cleanup spawning task marked as deleted
 *
 * Actually destroy task-structure waiting to be spawned but marked as
 * deleted. These tasks are expected to be marked via setting the
 * task's @ref deleted flag.
 *
 * @return No return value
 */
static void cleanupSpawnTasks(void)
{
    PSID_fdbg(PSID_LOG_VERB, "\n");

    list_t *t, *tmp;
    list_for_each_safe(t, tmp, &delayedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);

	if (task->deleted) {
	    PStasklist_dequeue(task);
	    PStask_delete(task);
	}
    }
}

/**
 * @brief Handle a PSP_CD_SPAWNSUCCESS message
 *
 * Handle the message @a msg of type PSP_CD_SPAWNSUCCESS.
 *
 * Register the spawned process to its parent task and forward the
 * message to the initiating process.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_SPAWNSUCCESS(DDErrorMsg_t *msg)
{
    PStask_ID_t tid = msg->header.sender;
    PStask_ID_t ptid = msg->header.dest;

    PSID_fdbg(PSID_LOG_SPAWN, "from %s", PSC_printTID(tid));
    PSID_dbg(PSID_LOG_SPAWN, " with parent(%s)\n", PSC_printTID(ptid));

    PStask_t *task = PStasklist_find(&managedTasks, ptid);
    if (task && (task->fd > -1 || task->group == TG_ANY)) {
	/* register the child */
	PSID_setSignal(&task->childList, tid, -1);
    } else {
	/* task not found, it has already died */
	PSID_flog("from %s", PSC_printTID(tid));
	PSID_log(" with parent(%s) already dead\n", PSC_printTID(ptid));
	PSID_sendSignal(tid, 0, ptid, -1,
			false /* pervasive */, false /* answer */);
	return true;
    }

    /*
     * Send the initiator the success message.
     *
     * If the initiator is a normal but unconnected task, it has used
     * its forwarder as a proxy via PMI. Thus, send SPAWNSUCCESS to
     * the proxy.
     */
    if (task && task->group == TG_ANY && task->fd == -1 && task->forwarder) {
	msg->header.dest = task->forwarder->tid;
    }
    sendMsg(msg);
    return true;
}

/**
 * @brief Handle a PSP_CD_SPAWNFAILED message
 *
 * Handle the message @a msg of type PSP_CD_SPAWNFAILED.
 *
 * This might have been created by a local forwarder. Thus, release
 * this forwarder and forwards the message to the initiating process.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_SPAWNFAILED(DDErrorMsg_t *msg)
{
    PSID_fdbg(PSID_LOG_SPAWN, "%s reports on rank %ld",
	      PSC_printTID(msg->header.sender), msg->request);
    PSID_dbg(PSID_LOG_SPAWN, " error = %d sending to parent %s\n", msg->error,
	     PSC_printTID(msg->header.dest));

    if (PSC_getID(msg->header.sender) == PSC_getMyID()) {
	/* Forwarder will disappear immediately, release it. */
	PStask_t *task = PStasklist_find(&managedTasks, msg->header.sender);

	if (!task) {
	    PSID_flog("task %s not found\n", PSC_printTID(msg->header.sender));
	} else {
	    task->released = true;
	    sendCHILDRESREL(task, msg->header.sender, false);
	    PSIDclient_delete(task->fd);
	}
    }

    /* send failure msg to initiator */
    sendMsg(msg);
    return true;
}

/**
 * @brief Handle a PSP_CD_SPAWNFINISH message
 *
 * Handle the message @a msg of type PSP_CD_SPAWNFINISH.
 *
 * This just forwards the message to the initiating process.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_SPAWNFINISH(DDMsg_t *msg)
{
    PSID_fdbg(PSID_LOG_SPAWN, "send to local parent %s\n",
	      PSC_printTID(msg->dest));

    /* send the initiator a finish msg */
    sendMsg(msg);
    return true;
}

/**
 * @brief Handle a PSP_DD_CHILDBORN message
 *
 * Handle the message @a msg of type PSP_DD_CHILDBORN.
 *
 * This type of message is created by the forwarder process to inform
 * the local daemon on the creation of the controlled client
 * process. This will result in setting up the daemon's
 * control-structures for tracing the child. Additionally the message
 * will be forwarded to the parent task in order to take according
 * measures.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_CHILDBORN(DDErrorMsg_t *msg)
{
    PStask_t *forwarder = PStasklist_find(&managedTasks, msg->header.sender);
    PStask_t *child = PStasklist_find(&managedTasks, msg->request);
    PStask_ID_t succMsgDest = 0;

    PSID_fdbg(PSID_LOG_SPAWN, "from %s\n", PSC_printTID(msg->header.sender));
    if (!forwarder) {
	PSID_flog("forwarder %s not found\n", PSC_printTID(msg->header.sender));
	return true;
    }

    if (child) {
	PSID_flog("child %s already there.", PSC_printTID(msg->request));
	PSID_log(" forwarder %s missed a message\n",
		 PSC_printTID(msg->header.sender));
	msg->header.type = PSP_DD_CHILDACK;
	msg->header.dest = msg->header.sender;
	msg->header.sender = PSC_getMyTID();
	sendMsg(msg);

	return true;
    }

    /* prepare child task */
    child = PStask_clone(forwarder);
    if (!child) {
	PSID_fwarn(errno, "PStask_clone()");

	char tasktxt[4096];
	PStask_snprintf(tasktxt, sizeof(tasktxt), forwarder);
	PSID_flog("forwarder %p: %s\n", forwarder, tasktxt);

	/* Tell forwarder to kill child */
	msg->header.type = PSP_DD_CHILDDEAD;
	msg->header.dest = msg->header.sender;
	msg->header.sender = PSC_getMyTID();
	sendMsg(msg);

	/* Tell parent about failure */
	msg->header.type = PSP_CD_SPAWNFAILED;
	msg->header.sender = msg->request;
	msg->header.dest = forwarder->ptid;
	msg->error = ENOMEM;
	msg->request = forwarder->rank;
	sendMsg(msg);
	sendCHILDRESREL(forwarder, PSC_getMyTID(), false);

	return true;
    }

    strvDestroy(forwarder->argV);
    forwarder->argV = NULL; // prevent use of re-initialized argV

    child->tid = msg->request;
    child->fd = -1;
    child->group = forwarder->childGroup;
    child->forwarder = forwarder;

    /* Accounting info */
    if (child->group != TG_ADMINTASK && child->group != TG_SERVICE
	&& child->group != TG_SERVICE_SIG && child->group != TG_KVS) {
	sendAcctChild(child);
    }

    /* Fix interactive shell's argv[0] */
    if (strvSize(child->argV) == 2) {
	if (!strvGet(child->argV, 0) || !strvGet(child->argV, 1)) {
	    PSID_flog("argV seems to be messed up\n");
	} else if (!strcmp(strvGet(child->argV, 0), "/bin/bash")
		   && !strcmp(strvGet(child->argV, 1), "-i")) {
	    strvDestroy(child->argV);
	    child->argV = strvNew(NULL);
	    strvAdd(child->argV, "-bash");
	} else if (!strcmp(strvGet(child->argV, 0), "/bin/tcsh")
		   && !strcmp(strvGet(child->argV, 1), "-i")) {
	    strvDestroy(child->argV);
	    child->argV = strvNew(NULL);
	    strvAdd(child->argV, "-tcsh");
	}
    }

    /* Enqueue the task right in front of the forwarder */
    PStasklist_enqueueBefore(&managedTasks, child, forwarder);
    /* Tell everybody about the new task */
    incTaskCount(child->group==TG_ANY);

    /* Spawned task will get signal if the forwarder dies unexpectedly. */
    PSID_setSignal(&forwarder->childList, child->tid, -1);

    /*
     * The answer will be sent directly to the initiator if he is on
     * the same node. Thus register directly as his child.
     */
    if (PSC_getID(child->ptid) == PSC_getMyID()) {
	PStask_t *parent = PStasklist_find(&managedTasks, child->ptid);

	if (!parent) {
	    PSID_flog("parent task %s not found\n", PSC_printTID(child->ptid));
	} else {
	    PSID_setSignal(&parent->childList, child->tid, -1);

	    /*
	     * If the parent is a normal but unconnected task, it was
	     * not the origin of the spawn but used the forwarder as a
	     * proxy via PMI. Thus, send SPAWNSUCCESS to the proxy.
	     */
	    if (parent->group == TG_ANY && parent->fd == -1
		&& parent->forwarder) {
		succMsgDest = parent->forwarder->tid;
	    }
	}
    }

    /* Tell forwarder to actually execv() client */
    msg->header.type = PSP_DD_CHILDACK;
    msg->header.dest = msg->header.sender;
    msg->header.sender = PSC_getMyTID();
    sendMsg(msg);

    /* Tell parent about success */
    msg->header.type = PSP_CD_SPAWNSUCCESS;
    msg->header.sender = child->tid;
    msg->header.dest = succMsgDest ? succMsgDest : child->ptid;
    msg->request = child->rank;
    msg->error = child->jobRank;
    sendMsg(msg);

    return true;
}

/**
 * @brief Handle a PSP_DD_CHILDDEAD message
 *
 * Handle the message @a msg of type PSP_DD_CHILDDEAD.
 *
 * This type of message is created by the forwarder process to inform
 * the local daemon on the dead of the controlled client process. This
 * might result in sending pending signals, un-registering the task,
 * etc. Additionally the message will be forwarded to the daemon
 * controlling the parent task in order to take according measures.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_CHILDDEAD(DDErrorMsg_t *msg)
{
    PSID_fdbg(PSID_LOG_SPAWN, "from %s", PSC_printTID(msg->header.sender));
    PSID_dbg(PSID_LOG_SPAWN, " to %s", PSC_printTID(msg->header.dest));
    PSID_dbg(PSID_LOG_SPAWN, " concerning %s\n", PSC_printTID(msg->request));

    bool obsoleteSndr = false;
    if (PSC_getID(msg->header.sender) == -2) {
	obsoleteSndr = true;
	msg->header.sender = PSC_getTID(-1, PSC_getPID(msg->header.sender));
    }

    /****** This part handles messages forwarded by some daemon ******/

    if (msg->header.dest != PSC_getMyTID()) {
	if (PSC_getID(msg->header.dest) != PSC_getMyID()) {
	    /* Destination on foreign node. Forward */
	    sendMsg(msg);
	    return true;
	}
	/* Destination on my node, let's take a peek */
	PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
	if (!task) return true;

	if (!PSID_removeSignal(&task->childList, msg->request, -1)) {
	    /* No child found. Might already be inherited by parent */
	    if (task->ptid) {
		msg->header.dest = task->ptid;
		msg->header.sender = PSC_getMyTID();

		PSID_fdbg(PSID_LOG_SPAWN, "forward PSP_DD_CHILDDEAD from %s",
			  PSC_printTID(msg->request));
		PSID_dbg(PSID_LOG_SPAWN, " dest %s", PSC_printTID(task->tid));
		PSID_dbg(PSID_LOG_SPAWN, "->%s\n", PSC_printTID(task->ptid));

		sendMsg(msg);
	    }
	    /* To be sure, mark child as dead */
	    PSID_fdbg(PSID_LOG_SPAWN, "%s not (yet?) child of",
		      PSC_printTID(msg->request));
	    PSID_dbg(PSID_LOG_SPAWN, " %s\n", PSC_printTID(task->tid));
	    PSID_setSignal(&task->deadBefore, msg->request, -1);
	}

	if (task->removeIt && PSID_emptySigList(&task->childList)) {
	    PSID_fdbg(PSID_LOG_TASK, "PSIDtask_cleanup()\n");
	    PSIDtask_cleanup(task);
	} else if (task->group == TG_SERVICE_SIG) {
	    /* service task requested signal */
	    if (!WIFEXITED(msg->error) || WIFSIGNALED(msg->error)
		|| PSID_emptySigList(&task->childList)) {
		PSID_sendSignal(task->tid, task->uid, msg->request, -1,
				false /* pervasive */, false /* answer */);
	    }
	}

	return true;
    }

    /****** This part handles original messages from a local forwarder ******/

    /* Release the corresponding forwarder */
    PStask_t *forwarder = PStasklist_find(
	obsoleteSndr ? &obsoleteTasks : &managedTasks, msg->header.sender);
    if (forwarder) {
	if (PSID_removeSignal(&forwarder->childList, msg->request, -1)) {
	    if (PSID_emptySigList(&forwarder->childList))
		forwarder->released = true;
	} else {
	    /* ensure non responsible forwarder is not referred any further */
	    forwarder = NULL;
	}
    } else {
	/* Forwarder not found */
	PSID_flog("forwarder %s not found\n", PSC_printTID(msg->header.sender));
    }

    /* Try to find the task */
    PStask_t *task = PStasklist_find(&managedTasks, msg->request);
    if (!task || task->forwarder != forwarder) {
	/* Maybe the task was obsoleted */
	task = PStasklist_find(&obsoleteTasks, msg->request);
	/* Still not the right task? */
	if (task && task->forwarder != forwarder) task = NULL;
    }

    if (!task) {
	/* task not found */
	/* This is not critical. Task has been removed by PSIDclient_delete() */
	PSID_fdbg(PSID_LOG_SPAWN, "task %s not found\n",
		  PSC_printTID(msg->request));
    } else if (!task->forwarder || task->forwarder != forwarder) {
	PSID_flog("forwarder %s", PSC_printTID(msg->header.sender));
	PSID_log(" irresponsible for %s\n", PSC_printTID(msg->request));
    } else {
	/** Create and send PSP_DD_CHILDRESREL message */
	sendCHILDRESREL(task, msg->request, true);
	PSCPU_clrAll(task->CPUset);

	/* Prepare CHILDDEAD msg here. Task might be removed in next step */
	if (!task->released) {
	    /* parent only expects CHILDDEAD if child is not released */
	    msg->header.dest = task->ptid;
	    msg->header.sender = PSC_getMyTID();
	}

	/* If child not connected, remove task from tasklist. This
	 * will also send all signals */
	if (task->fd == -1) {
	    PSID_fdbg(PSID_LOG_TASK, "PSIDtask_cleanup()\n");
	    PSIDtask_cleanup(task);
	}

	/* Send CHILDDEAD to parent */
	if (msg->header.dest != PSC_getMyTID()) msg_CHILDDEAD(msg);

	/* ensure task does no longer reference the forwarder */
	task->forwarder = NULL;
    }
    return true;
}

/**
 * @brief Check for obstinate tasks
 *
 * The purpose of this function is twice; one the one hand it checks
 * for obstinate tasks and sends SIGKILL signals until the task
 * disappears. On the other hand it garbage-collects all deleted task
 * structures within the list of managed tasks and frees them.
 *
 * @return No return value.
 */
static void checkObstinateTasks(void)
{
    time_t now = time(NULL);
    list_t *t, *tmp;

    list_for_each_safe(t, tmp, &managedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);

	if (task->deleted) {
	    /* If task is still connected, wait for connection closed */
	    if (task->fd == -1) {
		PStasklist_dequeue(task);
		PStask_delete(task);
	    }
	} else if (task->killat && now > task->killat) {
	    int ret;
	    if (task->group != TG_LOGGER) {
		/* Send the signal to the whole process group */
		ret = PSID_kill(-PSC_getPID(task->tid), SIGKILL, task->uid);
	    } else {
		/* Unless it's a logger, which will never fork() */
		ret = PSID_kill(PSC_getPID(task->tid), SIGKILL, task->uid);
	    }
	    if (ret && errno == ESRCH) {
		if (!task->removeIt) {
		    PSIDtask_cleanup(task);
		} else {
		    task->deleted = true;
		}
	    }
	}
    }
    list_for_each_safe(t, tmp, &obsoleteTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);

	if (task->deleted) {
	    /* If task is still connected, wait for connection closed */
	    if (task->fd == -1) {
		PStasklist_dequeue(task);
		PStask_delete(task);
	    }
	}
    }

}

int PSIDspawn_localTask(PStask_t *task, PSIDspawn_creator_t creator,
			Selector_CB_t *msgHandler)
{
    if (!task) return EINVAL;

    if (PSID_getDebugMask() & PSID_LOG_SPAWN) {
	char tasktxt[4096];
	PStask_snprintf(tasktxt, sizeof(tasktxt), task);
	PSID_flog("spawning %p: %s\n", task, tasktxt);
    }

    /* now try to start the task */
    int err = buildSandboxAndStart(creator ? creator : execForwarder, task);

    if (!err) {
	/* prepare and enqueue task */
	task->protocolVersion = PSProtocolVersion;
	if (!creator) {
	    /* we spawned a psidforwarder task */
	    /* prepare forwarder task */
	    task->childGroup = task->group;
	    task->group = TG_FORWARDER;
	} else {
	    /* No signals expected to be sent, thus, release immediately */
	    task->released = true;
	}
	PStasklist_enqueue(&managedTasks, task);
	/* The newly created process is already connected and established */
	PSIDclient_register(task->fd, task->tid, task);
	PSIDclient_setEstablished(task->fd, msgHandler, task);
	/* Tell everybody about the new forwarder task */
	incTaskCount(false);
    } else {
	PSID_fwarn(err, "buildSandboxAndStart()");
    }

    return err;
}

void PSIDspawn_init(void)
{
    PSID_fdbg(PSID_LOG_VERB, "\n");

    PSID_registerMsg(PSP_CD_SPAWNREQUEST, (handlerFunc_t) msg_SPAWNREQUEST);
    PSID_registerMsg(PSP_CD_SPAWNSUCCESS, (handlerFunc_t) msg_SPAWNSUCCESS);
    PSID_registerMsg(PSP_CD_SPAWNFAILED, (handlerFunc_t) msg_SPAWNFAILED);
    PSID_registerMsg(PSP_CD_SPAWNFINISH, (handlerFunc_t) msg_SPAWNFINISH);
    PSID_registerMsg(PSP_DD_CHILDDEAD, (handlerFunc_t) msg_CHILDDEAD);
    PSID_registerMsg(PSP_DD_CHILDBORN, (handlerFunc_t) msg_CHILDBORN);
    PSID_registerMsg(PSP_DD_CHILDACK, PSIDclient_frwd);

    PSID_registerDropper(PSP_CD_SPAWNREQUEST, (handlerFunc_t)drop_SPAWNREQUEST);

    PSID_registerLoopAct(checkObstinateTasks);
    PSID_registerLoopAct(cleanupSpawnTasks);
    PSID_registerLoopAct(sendAllPendCRR);

    PSIDhook_add(PSIDHOOK_CLEARMEM, clearMemPendCRR);
}
