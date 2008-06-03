/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
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
#include <sys/wait.h>
#include <pty.h>
#include <signal.h>
#include <syslog.h>
#define __USE_GNU
#include <sched.h>
#undef __USE_GNU
#ifdef HAVE_LIBNUMA
#include <numa.h>
#endif

#include "pscommon.h"
#include "pscpu.h"

#include "psidutil.h"
#include "psidforwarder.h"
#include "psidnodes.h"
#include "psidtask.h"
#include "psidcomm.h"
#include "psidclient.h"
#include "psidstatus.h"
#include "psidsignal.h"
#include "psidaccount.h"

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
    return ret ? ret : "UNKNOWN";
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
 * @brief Set up a new PMI tcp/ip socket and start listing.
 *
 * @param PMISocket the socket to init.
 *
 * @return Returns the initalized and listing socket.
 */
static int init_PMISocket(int PMISocket)
{
    int res;
    struct sockaddr_in saClient;

    PMISocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (PMISocket == 0) {
	PSID_warn(-1, errno, "%s: create PMIsock failed", __func__);
	exit(1);
    }

    /* set up the sockaddr structure */
    saClient.sin_family = AF_INET;
    saClient.sin_addr.s_addr = INADDR_ANY;
    saClient.sin_port = htons(0);
    bzero(&(saClient.sin_zero), 8);

    /* bind the socket */
    res = bind(PMISocket, (struct sockaddr *)&saClient, sizeof(saClient));

    if (res == -1) {
	PSID_warn(-1, errno, "%s: binding PMIsock failed", __func__);
	exit(1);
    }

    /* set socket to listen state */
    res = listen(PMISocket, 5);

    if (res == -1) {
	PSID_warn(-1, errno, "%s: listen on PMIsock failed", __func__);
	exit(1);
    }

    return PMISocket;
}

/**
 * @brief Set up the PMI_PORT variable.
 *
 * @param PMISock The PMI socket to get the information from.
 *
 * @param cPMI_PORT The buffer which receives the result.
 *
 * @param size The size of the cPMI_PORT buffer.
 *
 * @return No return value
 */
static void get_PMI_PORT(int PMISock, char *cPMI_PORT, int size )
{
    struct sockaddr_in addr;
    socklen_t len;

    /* get pmi port */
    len = sizeof(addr);
    bzero(&(addr), 8);
    if (getsockname(PMISock,(struct sockaddr*)&addr,&len) == -1) {
	PSID_warn(-1, errno, "%s: getsockname(pmisock)", __func__);
	exit(1);
    }

    snprintf(cPMI_PORT,size,"127.0.0.1:%i", ntohs(addr.sin_port));
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
    if (stat(tty, &st)) {
	PSID_exit(errno, "%s: stat(%s)", __func__, tty);
	exit(1);
    }

    if (st.st_uid != uid || st.st_gid != gid) {
	if (chown(tty, uid, gid) < 0) {
	    if (errno == EROFS && (st.st_uid == uid || st.st_uid == 0)) {
		PSID_warn(-1, errno, "%s: chown(%s, %u, %u)", __func__,
			  tty, (u_int)uid, (u_int)gid);
	    } else {
		PSID_exit(errno, "%s: chown(%s, %u, %u)", __func__,
			  tty, (u_int)uid, (u_int)gid);
	    }
	}
    }

    if ((st.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO)) != mode) {
	if (chmod(tty, mode) < 0) {
	    if (errno == EROFS && (st.st_mode & (S_IRGRP | S_IROTH)) == 0) {
		PSID_warn(-1, errno, "%s: chmod(%s, 0%o)", __func__,
			  tty, (u_int)mode);
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
    void *old;

    /* First disconnect from the old controlling tty. */
    fd = open(_PATH_TTY, O_RDWR | O_NOCTTY);
    if (fd >= 0) {
	if (ioctl(fd, TIOCNOTTY, NULL)<0)
	    PSID_warn(-1, errno, "%s: ioctl(TIOCNOTTY)", __func__);
	close(fd);
    }

    /*
     * Verify that we are successfully disconnected from the controlling
     * tty.
     */
    fd = open(_PATH_TTY, O_RDWR | O_NOCTTY);
    if (fd >= 0) {
	PSID_log(-1, "%s: still connected to controlling tty.\n", __func__);
	close(fd);
    }

    /* Make it our controlling tty. */
#ifdef TIOCSCTTY
    if (ioctl(*ttyfd, TIOCSCTTY, NULL) < 0)
	PSID_warn(-1, errno, "%s: ioctl(TIOCSCTTY)", __func__);
#else
#error No TIOCSCTTY
#endif /* TIOCSCTTY */

    old = signal(SIGCONT, SIG_IGN);
    if (vhangup() < 0) PSID_warn(-1, errno, "%s: vhangup()", __func__);
    signal(SIGCONT, old);

    fd = open(tty, O_RDWR);
    if (fd < 0) {
	PSID_warn(-1, errno, "%s: open(%s)", __func__, tty);
    } else {
	close(*ttyfd);
	*ttyfd = fd;
    }
    /* Verify that we now have a controlling tty. */
    fd = open(_PATH_TTY, O_WRONLY);
    if (fd < 0) {
	PSID_warn(-1, errno, "%s: open(%s)", __func__, _PATH_TTY);
	PSID_log(-1, "%s: unable to set controlling tty: %s\n",
		 __func__, _PATH_TTY);
    } else {
	close(fd);
    }
}

#ifdef CPU_ZERO
/**
 * @brief Bind process to node
 *
 * Bind the current process to all the NUMA nodes which contain cores
 * from within the set @a physSet.
 *
 * @param physSet A set of physical cores. The process is bound to the
 * NUMA nodes containing some of this cores.
 *
 * @return No return value.
 */
static void bindToNodes(cpu_set_t *physSet)
{
#ifdef HAVE_LIBNUMA
    int node;
    nodemask_t nodeset;

    if (numa_available()==-1) {
	fprintf(stderr, "NUMA not available. No binding\n");
	return;
    }

    nodemask_zero(&nodeset);

    /* Try to determine the nodes */
    for (node=0; node<=numa_max_node(); node++) {
	cpu_set_t CPUset;
	short cpu;
	int ret = numa_node_to_cpus(node,
				    (unsigned long*)&CPUset, sizeof(CPUset));
	if (ret) {
	    if (errno==ERANGE) {
		fprintf(stderr, "cpu_set_t to small for numa_node_to_cpus()");
	    } else {
		perror("numa_node_to_cpus()");
	    }
	    fprintf(stderr, "No binding\n");
	    return;
	}
	for (cpu=0; cpu<CPU_SETSIZE; cpu++) {
	    if (CPU_ISSET(cpu, physSet) && CPU_ISSET(cpu, &CPUset)) {
		nodemask_set(&nodeset, node);
	    }
	}
    }
    numa_set_membind(&nodeset);
#else
    fprintf(stderr, "Daemon not build against libnuma. No binding\n");
#endif
}

/**
 * @brief Pin process to cores
 *
 * Pin the process to the set of physical CPUs @a physSet.
 *
 * @param physSet The physical cores the process is pinned to.
 *
 * @return No return value.
 */
static void pinToCPUs(cpu_set_t *physSet)
{
    sched_setaffinity(0, sizeof(*physSet), physSet);
}

/**
 * @brief Map CPUs
 *
 * Map the logical CPUs of the CPU-set @a set to physical CPUs and
 * store them into the returned cpu_set_t as used by @ref
 * sched_setaffinity(), etc.
 *
 * @param set The set of CPUs to map.
 *
 * @return A set of physical CPUs is returned as a static set of type
 * cpu_set_t. Subsequent callls to @ref mapCPUs will modify this set.
 */
static cpu_set_t *mapCPUs(PSCPU_set_t set)
{
    short cpu, maxCPU = PSIDnodes_getPhysCPUs(PSC_getMyID());
    static cpu_set_t physSet;

    CPU_ZERO(&physSet);
    for (cpu=0; cpu<maxCPU; cpu++) {
	if (PSCPU_isSet(set, cpu)) {

	    short physCPU = PSIDnodes_mapCPU(PSC_getMyID(), cpu);
	    if (physCPU<0 || physCPU >= PSIDnodes_getVirtCPUs(PSC_getMyID())) {
		fprintf(stderr,
			"Mapping CPU %d->%d out of range. No pinning\n",
			cpu, physCPU);
		continue;
	    }
	    CPU_SET(physCPU, &physSet);
	}
    }

    {
	char txt[PSCPU_MAX+2];
	int i;
	for (i=maxCPU-1; i>=0; i--) {
	    if (CPU_ISSET(i, &physSet))
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "1");
	    else
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "0");
	}
	setenv("__PINNING__", txt, 1);
    }
    return &physSet;
}

#endif

/**
 * @brief Do various process clamps.
 *
 * Pin process to the logical CPU-set @a set and bind it to the NUMA
 * nodes serving these logical CPUs if demanded on the local
 * node. Therefore @ref pinToCPU() and @ref bindToNode() are called
 * respectively.
 *
 * Befor doing the actual pinning and binding the logical CPUs are
 * mapped to physical ones via mapCPUs().
 *
 * @param set The logical CPUs to pin and bind to.
 *
 * @return No return value.
 *
 * @see bindToNode(), pinToCPU(), mapCPUs()
 */
static void doClamps(PSCPU_set_t set)
{
    setenv("PSID_CPU_PINNING",  PSCPU_print(set), 1);

    int16_t physCPUs = PSIDnodes_getPhysCPUs(PSC_getMyID());

    if (!PSCPU_any(set, physCPUs)) {
	fprintf(stderr, "CPU slots not set. Old executable? "
		"You might want to relink your program.\n");
    } else if (PSCPU_all(set, physCPUs)) {
	/* No mapping */
    } else if (PSIDnodes_pinProcs(PSC_getMyID())
	       || PSIDnodes_bindMem(PSC_getMyID())) {
#ifdef CPU_ZERO
	cpu_set_t *physSet = mapCPUs(set);

	if (PSIDnodes_pinProcs(PSC_getMyID())) pinToCPUs(physSet);
	if (PSIDnodes_bindMem(PSC_getMyID())) bindToNodes(physSet);
#else
	fprintf(stderr, "Daemon has no sched_setaffinity(). No pinning\n");
#endif
    }
}

/**
 * @brief Actually start the client process.
 *
 * This function actually sets up the client process as described
 * within the task structure @a task. In order to do so, first the UID,
 * GID and the current working directory are set up correctly. If no
 * working directory is provided within @a task, the corresponding
 * user's home directory is used. After setting up the environment and
 * doing some tests on the existence and accessability of the
 * executable to call, finally the executable is called via @ref
 * myexecv().
 *
 * Error-messages are sent to stderr. It is the task of the calling
 * function to provide an environment to forward this message to the
 * end-user of this function.
 *
 * @param task The task structure describing the client process to set
 * up.
 *
 * @return No return value.
 *
 * @see fork(), errno
 */
static void execClient(PStask_t *task)
{
    /* logging is done via the forwarder thru stderr! */
    struct stat sb;
    int execFound = 0;
    char *executable;

    /* change the gid */
    if (setgid(task->gid)<0) {
	fprintf(stderr, "%s: setgid: %s\n", __func__, get_strerror(errno));
	exit(0);
    }

    /* remove psid's group memberships */
    setgroups(0, NULL);

    /* set supplementary groups if requested */
    if (PSIDnodes_supplGrps(PSC_getMyID())) {
	struct passwd *pw;
	if ((pw = getpwuid(task->uid)) && pw->pw_name) {
	    if (initgroups(pw->pw_name, task->gid) < 0) {
		fprintf(stderr, "%s: Cannot set supplementary groups: %s\n",
			__func__,  get_strerror(errno));
	    }
	}
    }

    /* change the uid */
    if (setuid(task->uid)<0) {
	fprintf(stderr, "%s: setuid: %s\n", __func__, get_strerror(errno));
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
		exit(0);
	    }
	} else {
	    fprintf(stderr, "Cannot determine home directory\n");
	    exit(0);
	}
    }

    doClamps(task->CPUset);

    if (!task->argv[0]) {
	fprintf(stderr, "No argv[0] given!\n");
	exit(0);
    }

    /* Test if executable is there */
    if (task->argv[0][0] != '/' && task->argv[0][0] != '.') {
	/* Relative path -> let's search in $PATH */
	char *p = getenv("PATH");

	if (!p) {
	    fprintf(stderr, "No path?\n");
	} else {
	    char *path = strdup(p);
	    p = strtok(path, ":");
	    while (p) {
		char *fn = PSC_concat(p, "/", task->argv[0], NULL);

		if (!stat(fn, &sb)) {
		    free(task->argv[0]);
		    task->argv[0] = fn;
		    execFound = 1;
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
	if (mystat(task->argv[0], &sb) == -1) {
	    fprintf(stderr, "%s: stat(%s): %s\n", __func__,
		    task->argv[0] ? task->argv[0] : "", get_strerror(errno));
	    exit(0);
	}
    }

    if (!S_ISREG(sb.st_mode) || !(sb.st_mode & S_IXUSR)) {
	errno = EACCES;
	fprintf(stderr, "%s: stat(): %s\n", __func__,
		(!S_ISREG(sb.st_mode)) ? "S_ISREG error" :
		(sb.st_mode & S_IXUSR) ? "" : "S_IXUSR error");
	exit(0);
    }

    executable = task->argv[0];

    /* Interactive shells */
    if (task->argc == 2 && (!strcmp(executable, "/bin/bash")
			     && !strcmp(task->argv[1], "-i"))) {
	task->argv[0] = "-bash";
	task->argv[1] = NULL;
	task->argc = 1;
    } else if (task->argc == 2 && (!strcmp(executable, "/bin/tcsh")
				   && !strcmp(task->argv[1], "-i"))) {
	task->argv[0] = "-tcsh";
	task->argv[1] = NULL;
	task->argc = 1;
    }

    /* execute the image */
    if (myexecv(executable, &(task->argv[0]))<0) {
	fprintf(stderr, "%s: execv: %s", __func__, get_strerror(errno));
    }
    /* never reached, if execv succesful */

    exit(0);
}

/**
 * @brief Reset signal handlers.
 *
 * Reset all the signal handlers. SIGCHLD needs special handling!
 *
 * @return No return value.
 */
static void resetSignals(void)
{
    signal(SIGHUP   ,SIG_DFL);
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
 * @brief Create fd pair
 *
 * Create a pair of file-descriptors @a fds acting as forwarder's
 * stdout or stderr connections to an actual client. The task
 * structure @a task determines in the member @a aretty if openpty()
 * or socketpair() is used to create the file-descriptor-pair. @a
 * fileNo is either STDOUT_FILENO or STDERR_FILENO and steers creation
 * of the corresponding channels. The file-descriptor @a cntrlCh is
 * used for flagging possible errors to a calling process.
 *
 * @param task Task structure determining the use of either openpty()
 * or socketpair().
 *
 * @param fds The pair of file-descriptors to create.
 *
 * @param fileNo Either STDOUT_FILENO or STDERR_FILENO.
 *
 * @param cntrlCh Possible errnos are send to this file-descriptor
 *
 * @return No return value.
 *
 * @see openpty(), socketpair()
 */
static void openChannel(PStask_t *task, int *fds, int fileNo, int cntrlCh)
{
    int ret;
    char *fdName = (fileNo == STDOUT_FILENO) ? "stdout" :
	(fileNo == STDERR_FILENO) ? "stderr" : "unknown";

    if (task->aretty & (1<<fileNo)) {
	if (openpty(&fds[0], &fds[1], NULL, &task->termios, &task->winsize)) {
	    PSID_warn(-1, errno, "%s: openpty(%s)", __func__, fdName);
	    ret = write(cntrlCh, &errno, sizeof(errno));
	    exit(1);
	}
    } else {
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, fds)) {
	    PSID_warn(-1, errno, "%s: socketpair(%s)", __func__, fdName);
	    ret = write(cntrlCh, &errno, sizeof(errno));
	    exit(1);
	}
    }
}

#define IDMAPFILE "/etc/elanidmap"

/**
 * @brief Verify Elan Host
 *
 * Verify if the host we are starting on
 * is listed in the elan config file. If not than
 * elan must be disabled, to prevent the libelan
 * from terminating us.
 *
 * @return No return value.
 */
static void verifyElanHost(void)
{
    FILE *elanIDfile;
    char line[256];
    char localhost[HOST_NAME_MAX];

    if ((gethostname(localhost, HOST_NAME_MAX)) == -1) {
	fprintf(stderr, "%s Error determining the local hostname\n", __func__);
	exit(1);
    }

    elanIDfile = fopen(IDMAPFILE, "r");

    if (!elanIDfile) {
	setenv("PSP_ELAN", "0", 1);
	return;
    }

    while (fgets(line, sizeof(line), elanIDfile)) {
	char *elanhost = strtok(line, " \t\n");

	if (!elanhost || *elanhost == '#') continue;

	if (!strcmp(elanhost, localhost)) {
	    fclose(elanIDfile);
	    return;
	}

    }

    setenv("PSP_ELAN", "0", 1);
    fclose(elanIDfile);
}

/**
 * @brief Create forwarder sandbox
 *
 * This function sets up a forwarder sandbox. Afterwards @ref
 * execClient() is called in order to start a client process within
 * this sandbox. The client process is described within the task
 * structure @a task.
 *
 * The forwarder will be connected to the local daemon from the very
 * beginning, i.e. it is not reconnecting. Therefor one end of a
 * socketpair of type Unix is needed in order to setup this
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
 * @param task The task structure describing the client process to set
 * up.
 *
 * @param daemonfd File descriptor representing one end of a Unix
 * socketpair used as a connection between forwarder and daemon.
 *
 * @param cntrlCh The control-channel the calling process should
 * listen to. This file-descriptor has to be one end of a pipe. The
 * calling process listens to the other end in order to detect
 * problems.
 *
 * @return No return value.
 *
 * @see fork(), errno
 */
static void execForwarder(PStask_t *task, int daemonfd, int cntrlCh)
{
    pid_t pid;
    int stdinfds[2], stdoutfds[2], stderrfds[2];
    int ret, buf;
    int PMISock = -1;
    int PMIforwarderSock = -1;
    int socketfds[2];
    int pmiEnableTcp = 0;
    int pmiEnableSockp = 0;
    PMItype_t pmiType = PMI_DISABLED;
    char *envstr;

    /* Block until the forwarder has handled all output */
    PSID_blockSig(1, SIGCHLD);

    if (task->aretty & (1<<STDIN_FILENO)
	&& task->aretty & (1<<STDOUT_FILENO)
	&& task->aretty & (1<<STDERR_FILENO)) task->interactive = 1;

    /* create stdin/stdout/stderr connections between forwarder & client */
    if (task->interactive) {
	openChannel(task, stderrfds, STDERR_FILENO, cntrlCh);
    } else {
	/* first stdout */
	openChannel(task, stdoutfds, STDOUT_FILENO, cntrlCh);

	/* then stderr */
	openChannel(task, stderrfds, STDERR_FILENO, cntrlCh);

	/*
	 * For stdin, use the stdout or stderr connection if the
	 * requested type is available, or open an extra connection.
	 */
	if (task->aretty & (1<<STDIN_FILENO)) {
	    if (task->aretty & (1<<STDERR_FILENO)) {
		stdinfds[0] = stderrfds[0];
		stdinfds[1] = stderrfds[1];
	    } else if (task->aretty & (1<<STDOUT_FILENO)) {
		stdinfds[0] = stdoutfds[0];
		stdinfds[1] = stdoutfds[1];
	    } else {
		if (openpty(&stdinfds[0], &stdinfds[1],
			    NULL, &task->termios, &task->winsize)) {
		    PSID_warn(-1, errno, "%s: openpty(stdin)", __func__);
		    ret = write(cntrlCh, &errno, sizeof(errno));
		    exit(1);
		}
	    }
	} else {
	    if (!(task->aretty & (1<<STDERR_FILENO))) {
		stdinfds[0] = stderrfds[0];
		stdinfds[1] = stderrfds[1];
	    } else if (!(task->aretty & (1<<STDOUT_FILENO))) {
		stdinfds[0] = stdoutfds[0];
		stdinfds[1] = stdoutfds[1];
	    } else {
		if (socketpair(PF_UNIX, SOCK_STREAM, 0, stdinfds)) {
		    PSID_warn(-1, errno, "%s: socketpair(stdin)", __func__);
		    ret = write(cntrlCh, &errno, sizeof(errno));
		    exit(1);
		}
	    }
	}
    }

    /* set some environment variables */
    /* this is done here in order to pass it to the forwarder, too */
    setenv("PWD", task->workingdir, 1);

    if (task->environ) {
	int i;
	for (i=0; task->environ[i]; i++) {
	    putenv(strdup(task->environ[i]));
	}
    }

    /* check if pmi should be started */
    if ((envstr = getenv("PMI_ENABLE_TCP"))) {
	pmiEnableTcp = atoi(envstr);
    }

    if ((envstr = getenv("PMI_ENABLE_SOCKP"))) {
	pmiEnableSockp = atoi(envstr);
    }

    /* only one option is allowed */
    if (pmiEnableSockp && pmiEnableTcp) {
	PSID_warn(-1, errno,
		  "%s: only one type of pmi connection allowed", __func__);
	exit(1);
    }

    /* open pmi socket for comm. between the pmi client and forwarder */
    if (pmiEnableTcp) {
	if (!(PMISock = init_PMISocket(PMISock))) {
	    PSID_warn(-1, errno, "%s: create PMI tcp socket failed", __func__);
	    exit(1);
	}
	PMIforwarderSock = PMISock;
	pmiType = PMI_OVER_TCP;
    }

    /* create a socketpair for comm. between the pmi client and forwarder */
    if (pmiEnableSockp) {
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, socketfds)<0) {
	    PSID_warn(-1, errno, "%s: socketpair()", __func__);
	    exit(1);
	}
	PMIforwarderSock = socketfds[1];
	pmiType = PMI_OVER_UNIX;
    }

    /* set some environment variables */
    /* this is done here in order to pass it to the forwarder, too */
    setenv("PWD", task->workingdir, 1);

    if (task->environ) {
	int i;
	for (i=0; task->environ[i]; i++) {
	    putenv(strdup(task->environ[i]));
	}
    }

    /* fork the client */
    if (!(pid = fork())) {
	/* this is the client process */
	char cPMI_PORT[50];
	char cPMI_FD[50];

	/* no direct connection to the daemon */
	close(daemonfd);

	/*
	 * Create a new process group. This is needed since the daemon
	 * kills whole process groups. Otherwise the daemon might
	 * also kill the forwarder by sending a signal to the client.
	 */
	if (setsid() < 0) {
	    PSID_warn(-1, errno, "%s: setsid()", __func__);
	}

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
	    close(stdinfds[0]);
	    task->stdin_fd = stdinfds[1];
	    close(stdoutfds[0]);
	    task->stdout_fd = stdoutfds[1];
	    close(stderrfds[0]);
	    task->stderr_fd = stderrfds[1];
	}

    	/* redirect stdin/stdout/stderr */
	if (dup2(task->stderr_fd, STDERR_FILENO) < 0) {
	    PSID_warn(-1, errno, "%s: dup2(stderr)", __func__);
	    exit(1);
	}

	/* From now on, all logging is done via the forwarder thru stderr */
	closelog();

	if (dup2(task->stdin_fd, STDIN_FILENO) < 0) {
	    fprintf(stderr, "%s: dup2(stdin): [%d] %s\n", __func__,
		    errno, get_strerror(errno));
	    exit(1);
	}
	if (dup2(task->stdout_fd, STDOUT_FILENO) < 0) {
	    fprintf(stderr, "%s: dup2(stdout): [%d] %s\n", __func__,
		    errno, get_strerror(errno));
	    exit(1);
	}

	/* close the now useless slave ttys / sockets */
	close(task->stderr_fd);
	if (!task->interactive) {
	    close(task->stdin_fd);
	    close(task->stdout_fd);
	}

	/* set the pmi port for the client to connect to the forwarder */
	if (pmiEnableTcp) {
	    get_PMI_PORT(PMISock, cPMI_PORT, sizeof(cPMI_PORT));
	    setenv("PMI_PORT", cPMI_PORT, 1);
	}

	/* set the pmi port for the client to connect to the forwarder */
	if (pmiEnableSockp) {
	    /* close forwarder socket */
	    close(socketfds[1]);
	    snprintf(cPMI_FD, sizeof(cPMI_FD), "%d", socketfds[0]);
	    setenv("PMI_FD", cPMI_FD, 1);
	}

	/* check if this node really supports elan */
	if ((envstr = getenv("PSP_ELAN")) && atoi(envstr)) {
	    verifyElanHost();
	}

	/* try to start the client */
	execClient(task);
    }

    /* this is the forwarder process */

    /* save errno in case of error */
    ret = errno;

    /* close the slave ttys / sockets */
    if (task->interactive) {
	close(stderrfds[1]);
	task->stdin_fd = stderrfds[0];
	task->stdout_fd = stderrfds[0];
    } else {
	close(stdinfds[1]);
	task->stdin_fd = stdinfds[0];
	close(stdoutfds[1]);
	task->stdout_fd = stdoutfds[0];
	close(stderrfds[1]);
	task->stderr_fd = stderrfds[0];
    }

    /* check if fork() was successful */
    if (pid == -1) {
	PSID_warn(-1, ret, "%s: fork()", __func__);
	ret = write(cntrlCh, &ret, sizeof(ret));
	exit(1);
    }

    /* check for a sign from the client */
    PSID_log(PSID_LOG_SPAWN, "%s: waiting for my child (%d)\n", __func__, pid);

    /* Tell the parent about the client's pid */
    buf = 0; /* errno will never be 0, this marks the following pid */
    ret = write(cntrlCh, &buf, sizeof(buf));
    buf = pid;
    ret = write(cntrlCh, &buf, sizeof(buf));

    resetSignals();

    /* Pass the client's PID to the forwarder. */
    task->tid = PSC_getTID(-1, pid);

    /* Release the waiting daemon and exec forwarder */
    close(cntrlCh);
    PSID_forwarder(task, daemonfd, PMIforwarderSock, pmiType,
		   PSID_getNumAcct(), PSIDnodes_acctPollI(PSC_getMyID()));

    /* never reached */
    exit(1);
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
	DDTypedBufferMsg_t msg;
	char *ptr = msg.buf;

	msg.header.type = PSP_CD_ACCOUNT;
	msg.header.dest = PSC_getMyTID();
	msg.header.sender = task->tid;
	msg.header.len = sizeof(msg.header);

	msg.type = PSP_ACCOUNT_CHILD;
	msg.header.len += sizeof(msg.type);

	/* logger's TID, this identifies a task uniquely */
	*(PStask_ID_t *)ptr = task->loggertid;
	ptr += sizeof(PStask_ID_t);
	msg.header.len += sizeof(PStask_ID_t);

	/* current rank */
	*(int32_t *)ptr = task->rank;
	ptr += sizeof(int32_t);
	msg.header.len += sizeof(int32_t);

	/* child's uid */
	*(uid_t *)ptr = task->uid;
	ptr += sizeof(uid_t);
	msg.header.len += sizeof(uid_t);

	/* child's gid */
	*(gid_t *)ptr = task->gid;
	ptr += sizeof(gid_t);
	msg.header.len += sizeof(gid_t);

#define MAXARGV0 128
	/* job's name */
	if (task->argv && task->argv[0]) {
	    size_t len = strlen(task->argv[0]), offset=0;

	    if (len > MAXARGV0) {
		strcpy(ptr, "...");
		ptr += 3;
		msg.header.len += 3;

		offset = len-MAXARGV0+3;
		len = MAXARGV0-3;
	    }
	    strcpy(ptr, &task->argv[0][offset]);
	    ptr += len;
	    msg.header.len += len;
	}
        *ptr = '\0';
        ptr++;
        msg.header.len++;

	sendMsg((DDMsg_t *)&msg);
}

/**
 * @brief Build sandbox and spawn process within.
 *
 * Build a new sandbox and spawn the process described by @a client
 * within. In order to do this, first of all a forwarder is created
 * that sets up a sandbox for the client process to run in. The the
 * actual client process is started within this sandbox.
 *
 * All necessary information determined during startup of the
 * forwarder and client process is stored within the corresponding
 * task structures. For the forwarder this includes the task ID and
 * the file descriptor connecting the local daemon to the
 * forwarder. For the client only the task ID is stored.
 *
 * @param forwarder Task structure describing the forwarder process to
 * create.
 *
 * @param client Task structure describing the actual client process
 * to create.
 *
 * @return On success, 0 is returned. If something went wrong, a value
 * different from 0 is returned. This value might be interpreted as an
 * errno describing the problem that occurred during the spawn.
 */
static int buildSandboxAndStart(PStask_t *forwarder, PStask_t *client)
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
	/* Start with connection to syslog */
	closelog();
	/* Then all the rest */
	for (i=0; i<getdtablesize(); i++) {
	    if (i!=STDIN_FILENO && i!=STDOUT_FILENO && i!=STDERR_FILENO
		&& i!=forwarderfds[1] && i!=socketfds[1]) {
		close(i);
	    }
	}

	/* Reopen the syslog and rename the tag */
	openlog("psidforwarder", LOG_PID|LOG_CONS, config->logDest);

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
	PSID_warn(-1, errno, "%s: read() failed", __func__);
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

    /* Accounting info */
    if (!ret && client->group != TG_ADMINTASK && client->group != TG_SERVICE) {
	sendAcctChild(client);
    }

    /* Fix interactive shell's argv[0] */
    if (client->argc == 2 && (!strcmp(client->argv[0], "/bin/bash")
			      && !strcmp(client->argv[1], "-i"))) {
	free(client->argv[0]);
	client->argv[0] = strdup("-bash");
	free(client->argv[1]);
	client->argv[1] = NULL;
	client->argc = 1;
    } else if (client->argc == 2 && (!strcmp(client->argv[0], "/bin/tcsh")
				     && !strcmp(client->argv[1], "-i"))) {
	free(client->argv[0]);
	client->argv[0] = strdup("-tcsh");
	free(client->argv[1]);
	client->argv[1] = NULL;
	client->argc = 1;
    }

    return ret;
}

/** Chunksize for PSP_ACCOUNT_SLOTS messages */
#define ACCT_SLOTS_CHUNK 128

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
    DDTypedBufferMsg_t msg;
    char *ptr = msg.buf;
    unsigned int slot;

    msg.header.type = PSP_CD_ACCOUNT;
    msg.header.dest = PSC_getMyTID();
    msg.header.sender = sender;
    msg.header.len = sizeof(msg.header);

    msg.type = PSP_ACCOUNT_START;
    msg.header.len += sizeof(msg.type);

    /* logger's TID, this identifies a task uniquely */
    *(PStask_ID_t *)ptr = task->loggertid;
    ptr += sizeof(PStask_ID_t);
    msg.header.len += sizeof(PStask_ID_t);

    /* current rank */
    *(int32_t *)ptr = task->rank;
    ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    /* child's uid */
    *(uid_t *)ptr = task->uid;
    ptr += sizeof(uid_t);
    msg.header.len += sizeof(uid_t);

    /* child's gid */
    *(gid_t *)ptr = task->gid;
    ptr += sizeof(gid_t);
    msg.header.len += sizeof(gid_t);

    /* total number of childs */
    *(int32_t *)ptr = task->partitionSize;
    ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    sendMsg((DDMsg_t *)&msg);

    msg.type = PSP_ACCOUNT_SLOTS;

    for (slot = 0; slot < task->partitionSize; slot++) {
	if (! (slot%ACCT_SLOTS_CHUNK)) {
	    if (slot) sendMsg((DDMsg_t *)&msg);

	    msg.header.len = sizeof(msg.header) + sizeof(msg.type);

	    ptr = msg.buf + sizeof(PStask_ID_t);
	    msg.header.len += sizeof(PStask_ID_t);

	    *(uint32_t *)ptr =
		(task->partitionSize - slot < ACCT_SLOTS_CHUNK) ?
		task->partitionSize - slot : ACCT_SLOTS_CHUNK;
	    ptr += sizeof(uint32_t);
	    msg.header.len += sizeof(uint32_t);
	}

	memcpy(ptr, &task->partition[slot].node, sizeof(PSnodes_ID_t));
	ptr += sizeof(PSnodes_ID_t);
	msg.header.len += sizeof(PSnodes_ID_t);
	memcpy(ptr, task->partition[slot].CPUset, sizeof(PSCPU_set_t));
	ptr += sizeof(PSCPU_set_t);
	msg.header.len += sizeof(PSCPU_set_t);
    }

    sendMsg((DDMsg_t *)&msg);
}


/**
 * @brief Check spawn request
 *
 * Check if the spawnrequest as decoded in @a task is correct, i.e. if
 * it does not violate some security measures or node configurations.
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
    PStask_t *ptask;

    if (sender != task->ptid) {
	/* Sender has to be parent */
	PSID_log(-1, "%s: spawner tries to cheat\n", __func__);
	return EACCES;
    }

    ptask = PStasklist_find(managedTasks, task->ptid);

    if (!ptask) {
	/* Parent not found */
	PSID_log(-1, "%s: parent task not found\n", __func__);
	return EACCES;
    }

    if (ptask->uid && task->uid!=ptask->uid) {
	/* Spawn tries to change uid */
	PSID_log(-1, "%s: try to setuid() task->uid %d  ptask->uid %d\n",
		 __func__, task->uid, ptask->uid);
	return EACCES;
    }

    if (ptask->gid && task->gid!=ptask->gid) {
	/* Spawn tries to change gid */
	PSID_log(-1, "%s: try to setgid() task->gid %d  ptask->gid %d\n",
		 __func__, task->gid, ptask->gid);
	return EACCES;
    }

    if (!PSIDnodes_isStarter(PSC_getMyID()) && task->group != TG_ADMINTASK
	&& (ptask->group == TG_SPAWNER || ptask->group == TG_PSCSPAWNER
	    || ptask->group == TG_LOGGER)) {
	/* starting not allowed */
	PSID_log(-1, "%s: spawning not allowed\n", __func__);
	return EACCES;
    }

    if (task->group == TG_ADMINTASK
	&& !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMUSER,
			       (PSIDnodes_guid_t){.u=ptask->uid})
	&& !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMGROUP,
			       (PSIDnodes_guid_t){.g=ptask->gid})) {
	/* no permission to start admin task */
	PSID_log(-1, "%s: no permission to spawn admintask\n", __func__);
	return EACCES;
    }

    PSID_log(PSID_LOG_SPAWN, "%s: request from %s ok\n", __func__,
	     PSC_printTID(task->ptid));

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
 * that sets up a sandbox for the client process to run in. The the
 * actual client process is started within this sandbox.
 *
 * All necessary information determined during startup of the
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
    PStask_t *forwarder;
    int err;

    if (!task) return EINVAL;

    /* prepare forwarder task */
    forwarder = PStask_clone(task);
    if (forwarder->argv) {
	int i;
	for (i=0; i<forwarder->argc; i++) {
	    if (forwarder->argv[i]) free(forwarder->argv[i]);
	}
	free(forwarder->argv);
	forwarder->argv = NULL;
	forwarder->argc = 0;
    }
    forwarder->group = TG_FORWARDER;
    forwarder->protocolVersion = PSProtocolVersion;

    /* now try to start the task */
    err = buildSandboxAndStart(forwarder, task);

    if (!err) {
	/* Task will get signal from parent. Thus add ptid to assignedSigs */
	PSID_setSignal(&task->assignedSigs, task->ptid, -1);
	/* Enqueue the task */
	PStasklist_enqueue(&managedTasks, task);
	/* Tell everybody about the new task */
	incJobs(1, (task->group==TG_ANY));

	/* Spawned task will get signal if the forwarder dies unexpectedly. */
	PSID_setSignal(&forwarder->childs, task->tid, -1);
	/* Enqueue the forwarder */
	PStasklist_enqueue(&managedTasks, forwarder);
	/* The forwarder is already connected and established */
	registerClient(forwarder->fd, forwarder->tid, forwarder);
	setEstablishedClient(forwarder->fd);
	FD_SET(forwarder->fd, &PSID_readfds);
	/* Tell everybody about the new forwarder task */
	incJobs(1, (forwarder->group==TG_ANY));

	/*
	 * The answer will be sent directly to the initiator if he is on
	 * the same node. Thus register directly as his child.
	 */
	if (PSC_getID(task->ptid) == PSC_getMyID()) {
	    PStask_t *parent = PStasklist_find(managedTasks, task->ptid);

	    if (!parent) {
		PSID_log(-1, "%s: parent task %s not found\n", __func__,
			 PSC_printTID(task->ptid));
	    } else {
		PSID_setSignal(&parent->childs, task->tid, -1);
		PSID_setSignal(&parent->assignedSigs, task->tid, -1);
	    }
	}
    } else {
	PSID_warn(PSID_LOG_SPAWN, err, "%s: buildSandboxAndStart()", __func__);

	PStask_delete(forwarder);
	PStask_delete(task);
    }

    return err;
}

void msg_SPAWNREQUEST(DDBufferMsg_t *msg)
{
    PStask_t *task;
    DDErrorMsg_t answer;

    char tasktxt[128];

    task = PStask_new();

    PStask_decodeFull(msg->buf, task);

    PStask_snprintf(tasktxt, sizeof(tasktxt), task);
    PSID_log(PSID_LOG_SPAWN, "%s: from %s msglen %d task %s\n", __func__,
	     PSC_printTID(msg->header.sender), msg->header.len, tasktxt);

    answer.header.dest = msg->header.sender;
    answer.header.sender = PSC_getMyTID();
    answer.header.len = sizeof(answer);

    /* If message is from my node, test if everything is okay */
    if (PSC_getID(msg->header.sender)==PSC_getMyID()) {
	answer.error = checkRequest(msg->header.sender, task);

	if (answer.error) {
	    PStask_delete(task);

	    answer.header.type = PSP_CD_SPAWNFAILED;

	    sendMsg(&answer);

	    return;
	}
    }

    if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
	answer.error = spawnTask(task);

	answer.header.type =
	    (answer.error ? PSP_CD_SPAWNFAILED : PSP_CD_SPAWNSUCCESS);
	if (!answer.error) answer.header.sender = task->tid;

	/* send the existence or failure of the request */
	sendMsg(&answer);
    } else {
	/* request for a remote site. */
	if (!PSIDnodes_isUp(PSC_getID(msg->header.dest))) {
	    send_DAEMONCONNECT(PSC_getID(msg->header.dest));
	}

	PSID_log(PSID_LOG_SPAWN, "%s: forwarding to node %d\n",
		 __func__, PSC_getID(msg->header.dest));

	if (sendMsg(msg) < 0) {
	    answer.header.type = PSP_CD_SPAWNFAILED;
	    answer.header.sender = msg->header.dest;
	    answer.error = errno;

	    sendMsg(&answer);
	}

	PStask_delete(task);
    }
}

/**
 * List of all tasks waiting to get spawned, i.e. waiting for last
 * environment packets to come in.
 */
static PStask_t *spawnTasks = NULL;

void msg_SPAWNREQ(DDTypedBufferMsg_t *msg)
{
    PStask_t *task, *ptask = NULL;
    DDErrorMsg_t answer;
    size_t usedBytes;
    int32_t rank = -1;
    PStask_group_t group = TG_ANY;

    char tasktxt[128];

    PSID_log(PSID_LOG_SPAWN, "%s: from %s msglen %d\n", __func__,
	     PSC_printTID(msg->header.sender), msg->header.len);

    answer.header.dest = msg->header.sender;
    answer.header.sender = PSC_getMyTID();
    answer.header.len = sizeof(answer);
    answer.error = 0;

    /* If message is from my node, test if everything is okay */
    if (PSC_getID(msg->header.sender)==PSC_getMyID()
	&& msg->type == PSP_SPAWN_TASK) {
	task = PStask_new();
	PStask_decodeTask(msg->buf, task);
	answer.error = checkRequest(msg->header.sender, task);

	if (answer.error) {
	    PStask_delete(task);
	    answer.header.type = PSP_CD_SPAWNFAILED;
	    answer.header.sender = msg->header.dest;
	    sendMsg(&answer);

	    return;
	}
	/* Store some info from task for latter usage */
	group = task->group;
	rank = task->rank;

	PStask_delete(task);

	/* Since checkRequest() did not fail, we will find ptask */
	ptask = PStasklist_find(managedTasks, msg->header.sender);
    }


    if (PSC_getID(msg->header.dest) != PSC_getMyID()) {
	/* request for a remote site. */
	int destDaemonPSPver =
	    PSIDnodes_getDaemonProtoVersion(PSC_getID(msg->header.dest));

	if (!PSIDnodes_isUp(PSC_getID(msg->header.dest))) {
	    send_DAEMONCONNECT(PSC_getID(msg->header.dest));
	}

	PSID_log(PSID_LOG_SPAWN, "%s: forwarding to node %d\n",
		 __func__, PSC_getID(msg->header.dest));

	if (sendMsg(msg) < 0) {
	    answer.header.type = PSP_CD_SPAWNFAILED;
	    answer.header.sender = msg->header.dest;
	    answer.error = errno;

	    sendMsg(&answer);
	}

	if (PSC_getID(msg->header.sender)==PSC_getMyID()
	    && msg->type == PSP_SPAWN_TASK
	    && group != TG_SERVICE && group != TG_ADMINTASK) {
	    if (!ptask->spawnNodes || rank >= ptask->spawnNum) {
		PSID_log(-1, "%s: rank %d out of range\n", __func__, rank);
	    } else {
		/** Create and send PSP_SPAWN_LOC message */
		DDTypedBufferMsg_t locMsg = (DDTypedBufferMsg_t) {
		    .header = (DDMsg_t) {
			.type = PSP_CD_SPAWNREQ,
			.dest = msg->header.dest,
			.sender = msg->header.sender,
			.len = sizeof(locMsg.header) + sizeof(locMsg.type)},
		    .type = PSP_SPAWN_LOC };
		char *ptr = locMsg.buf;

		if (destDaemonPSPver < 401) {
		    *(int16_t *)ptr =
			PSCPU_first(ptask->spawnNodes[rank].CPUset,
				    PSIDnodes_getPhysCPUs(
					PSC_getID(locMsg.header.dest)));
		    ptr += sizeof(int16_t);
		    locMsg.header.len += sizeof(int16_t);
		} else {
		    memcpy(ptr, ptask->spawnNodes[rank].CPUset,
			   sizeof(PSCPU_set_t));
		    ptr += sizeof(PSCPU_set_t);
		    locMsg.header.len += sizeof(PSCPU_set_t);
		}

		PSID_log(PSID_LOG_SPAWN, "%s: send PSP_SPAWN_LOC to node %d\n",
			 __func__, PSC_getID(locMsg.header.dest));

		if (sendMsg(&locMsg) < 0) {
		    PSID_warn(-1, errno,
			      "%s: send PSP_SPAWN_LOC to node %d failed",
			      __func__, PSC_getID(locMsg.header.dest));
		}
	    }
	}
	return;
    }

    task = PStasklist_find(spawnTasks, msg->header.sender);

    switch (msg->type) {
    case PSP_SPAWN_TASK:
	if (task) {
	    PStask_snprintf(tasktxt, sizeof(tasktxt), task);
	    PSID_log(-1, "%s: from %s task %s allready there\n",
		     __func__, PSC_printTID(msg->header.sender), tasktxt);
	    return;
	}
	task = PStask_new();
	PStask_decodeTask(msg->buf, task);
	task->tid = msg->header.sender;

	PStask_snprintf(tasktxt, sizeof(tasktxt), task);
	PSID_log(PSID_LOG_SPAWN, "%s: create %s\n", __func__, tasktxt);

	PStasklist_enqueue(&spawnTasks, task);

	if (task->group == TG_SERVICE || task->group == TG_ADMINTASK) {
	    PSCPU_setAll(task->CPUset);
	} else if (PSC_getID(msg->header.sender)==PSC_getMyID()) {
	    if (!ptask->spawnNodes || rank >= ptask->spawnNum) {
		PSID_log(-1, "%s: rank %d out of range\n", __func__, rank);
	    } else {
		memcpy(task->CPUset,
		       ptask->spawnNodes[rank].CPUset, sizeof(task->CPUset));
	    }
	}
	return;
	break;
    case PSP_SPAWN_LOC:
    {
	int srcDaemonPSPver =
	    PSIDnodes_getDaemonProtoVersion(PSC_getID(msg->header.sender));

	if (!task) {
	    PSID_log(-1, "%s: PSP_SPAWN_LOC from %s: task not found\n",
		     __func__, PSC_printTID(msg->header.sender));
	    return;
	}
	if (srcDaemonPSPver < 401) {
	    PSCPU_setCPU(task->CPUset, *(int16_t *)msg->buf);
	    usedBytes = sizeof(int16_t);
	} else {
	    memcpy(task->CPUset, msg->buf, sizeof(task->CPUset));
	    usedBytes = sizeof(task->CPUset);
	}
	break;
    }
    case PSP_SPAWN_ARG:
	if (!task) {
	    PSID_log(-1, "%s: PSP_SPAWN_ARG from %s: task not found\n",
		     __func__, PSC_printTID(msg->header.sender));
	    return;
	}
	usedBytes = PStask_decodeArgs(msg->buf, task);
	break;
    case PSP_SPAWN_ENV:
    case PSP_SPAWN_END:
	if (!task) {
	    PSID_log(-1, "%s: PSP_SPAWN_EN[V|D] from %s: task not found\n",
		     __func__, PSC_printTID(msg->header.sender));
	    return;
	}
	usedBytes = PStask_decodeEnv(msg->buf, task);
	break;
    case PSP_SPAWN_ENVCNTD:
	if (!task) {
	    PSID_log(-1, "%s: PSP_SPAWN_ENVCTND from %s: task not found\n",
		     __func__, PSC_printTID(msg->header.sender));
	    return;
	}
	usedBytes = PStask_decodeEnvAppend(msg->buf, task);
	break;
    default:
	PSID_log(-1, "%s: Unknown type '%d'\n", __func__, msg->type);
	return;
    }

    if (msg->header.len-sizeof(msg->header)-sizeof(msg->type) != usedBytes) {
	PSID_log(-1, "%s: problem decoding task %s type %d used %ld of %ld\n",
		 __func__, PSC_printTID(msg->header.sender), msg->type,
		 (long) usedBytes,
		 (long) msg->header.len-sizeof(msg->header)-sizeof(msg->type));
	return;
    }

    if (msg->type == PSP_SPAWN_END) {
	PStask_snprintf(tasktxt, sizeof(tasktxt), task);
	PSID_log(PSID_LOG_SPAWN, "%s: Spawning %s\n", __func__, tasktxt);

	PStasklist_dequeue(&spawnTasks, task->tid);
	answer.error = spawnTask(task);

	answer.header.type =
	    (answer.error ? PSP_CD_SPAWNFAILED : PSP_CD_SPAWNSUCCESS);
	if (!answer.error) answer.header.sender = task->tid;

	/* send the existence or failure of the request */
	sendMsg(&answer);
    }
}

void msg_SPAWNSUCCESS(DDErrorMsg_t *msg)
{
    PStask_ID_t tid = msg->header.sender;
    PStask_ID_t ptid = msg->header.dest;
    PStask_t *task;
    char *parent = strdup(PSC_printTID(ptid));

    PSID_log(PSID_LOG_SPAWN, "%s(%s) with parent(%s)\n",
	     __func__, PSC_printTID(tid), parent);

    task = PStasklist_find(managedTasks, ptid);
    if (task) {
	/* register the child */
	PSID_setSignal(&task->childs, tid, -1);

	/* child will send a signal on exit, thus include into assignedSigs */
	PSID_setSignal(&task->assignedSigs, tid, -1);
    } else {
	/* task not found, it has already died */
	PSID_log(-1, "%s(%s) with parent(%s) already dead\n",
		 __func__, PSC_printTID(tid), parent);
	PSID_sendSignal(tid, 0, ptid, -1, 0, 0);
    }

    /* send the initiator the success msg */
    sendMsg(msg);
    free(parent);
}

void msg_SPAWNFAILED(DDErrorMsg_t *msg)
{
    PSID_log(PSID_LOG_SPAWN, "%s: error = %d sending to local parent %s\n",
	     __func__, msg->error, PSC_printTID(msg->header.dest));

    /* send the initiator the failure msg */
    sendMsg(msg);
}

void msg_SPAWNFINISH(DDMsg_t *msg)
{
    PSID_log(PSID_LOG_SPAWN, "%s: sending to local parent %s\n",
	     __func__, PSC_printTID(msg->dest));

    /* send the initiator a finish msg */
    sendMsg(msg);
}

void msg_CHILDDEAD(DDErrorMsg_t *msg)
{
    PStask_t *task, *forwarder;

    PSID_log(PSID_LOG_SPAWN, "%s: from %s", __func__,
	     PSC_printTID(msg->header.sender));
    PSID_log(PSID_LOG_SPAWN, " to %s", PSC_printTID(msg->header.dest));
    PSID_log(PSID_LOG_SPAWN, " concerning %s\n", PSC_printTID(msg->request));

    if (msg->header.dest != PSC_getMyTID()) {
	/* Not for me, thus forward it. But first take a peek */
	if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
	    /* dest is on my node */
	    task = PStasklist_find(managedTasks, msg->header.dest);

	    if (!task) return;

	    /* Remove dead child from list of childs */
	    PSID_removeSignal(&task->assignedSigs, msg->request, -1);
	    PSID_removeSignal(&task->childs, msg->request, -1);

	    if (task->removeIt && !task->childs) {
		PSID_log(PSID_LOG_TASK, "%s: PStask_cleanup()\n", __func__);
		PStask_cleanup(task->tid);
		return;
	    }

	    /* Release a TG_(PSC)SPAWNER if child died in a fine way */
	    if (WIFEXITED(msg->error) && !WIFSIGNALED(msg->error)) {
		if (task->group == TG_SPAWNER || task->group == TG_PSCSPAWNER)
		    task->released = 1;
	    }

	    switch (task->group) {
	    case TG_SPAWNER:
	    case TG_GMSPAWNER:
		/* Do not send a DD message to a client */
		msg->header.type = PSP_CD_SPAWNFINISH;
		break;
	    case TG_SERVICE:
		/* TG_SERVICE expects signal, not message */
		if (!WIFEXITED(msg->error) || WIFSIGNALED(msg->error)
		    || !task->childs) {
		    PSID_sendSignal(task->tid, task->uid,
				    msg->request, -1, 0, 0);
		}
	    default:
		/* Don't send message if task not TG_(GM)SPAWNER */
		return;
	    }
	}
	sendMsg(msg);

	return;
    }

    /* Release the corresponding forwarder */
    forwarder = PStasklist_find(managedTasks, msg->header.sender);
    if (forwarder) {
	forwarder->released = 1;
	PSID_removeSignal(&forwarder->childs, msg->request, -1);
    } else {
	/* Forwarder not found */
	PSID_log(-1, "%s: forwarder task %s not found\n",
		 __func__, PSC_printTID(msg->header.sender));
    }

    /* Try to find the task */
    task = PStasklist_find(managedTasks, msg->request);

    if (!task) {
	/* task not found */
	/* This is not critical. Task has been removed by deleteClient() */
	PSID_log(PSID_LOG_SPAWN, "%s: task %s not found\n", __func__,
		 PSC_printTID(msg->request));
    } else {
	/* Check the status */
	if (WIFEXITED(msg->error) && !WEXITSTATUS(msg->error)
	    && !WIFSIGNALED(msg->error)) {
	    /* and release the task if no error occurred */
	    task->released = 1;
	}

	/*
	 * Send a SIGKILL to the process group in order to stop fork()ed childs
	 *
	 * Don't send to logger. These might share their process group
	 * with other processes. Furthermore logger never fork().
	 */
	if (task->group != TG_LOGGER) {
	    PSID_kill(-PSC_getPID(task->tid), SIGKILL, task->uid);
	}

	/* Send a message to the parent (a TG_(GM)SPAWNER might wait for it) */
	msg->header.dest = task->ptid;
	msg->header.sender = PSC_getMyTID();
	msg_CHILDDEAD(msg);

	/* If child not connected, remove task from tasklist */
	if (task->fd == -1) {
	    PSID_log(PSID_LOG_TASK, "%s: PStask_cleanup()\n", __func__);
	    PStask_cleanup(msg->request);
	}
    }
}
