/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
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
#include <sys/resource.h>
#include <sys/wait.h>
#include <pty.h>
#include <signal.h>
#include <syslog.h>
#ifdef HAVE_LIBNUMA
#include <numa.h>
#endif
#include <limits.h>
#include <sys/select.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "psprotocolenv.h"
#include "psdaemonprotocol.h"
#include "pscpu.h"
#include "selector.h"
#include "timer.h"

#include "psidutil.h"
#include "psidforwarder.h"
#include "psidnodes.h"
#include "psidtask.h"
#include "psidcomm.h"
#include "psidclient.h"
#include "psidstatus.h"
#include "psidsignal.h"
#include "psidaccount.h"
#include "psidhook.h"

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

    PSID_warn(-1, eno, "%s: %s()", __func__, alarmFunc);
    fprintf(stderr, "%s: %s(): %s\n", __func__, alarmFunc, get_strerror(eno));

    if (alarmFD >= 0) {
	int ret = write(alarmFD, &eno, sizeof(eno));
	if (ret < 0) {
	    eno = errno;
	    PSID_warn(-1, eno, "%s: write()", __func__);
	    fprintf(stderr, "%s: write(): %s\n", __func__, get_strerror(eno));
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
    void *oldCONT, *oldHUP;

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

    oldCONT = signal(SIGCONT, SIG_IGN);
    oldHUP = signal(SIGHUP, SIG_IGN);
    if (vhangup() < 0) PSID_warn(-1, errno, "%s: vhangup()", __func__);
    signal(SIGCONT, oldCONT);
    signal(SIGHUP, oldHUP);

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
void PSID_bindToNodes(cpu_set_t *physSet)
{
#ifdef HAVE_LIBNUMA
    int node, ret = 1;
#ifdef HAVE_NUMA_ALLOCATE_NODEMASK
    struct bitmask *nodemask = NULL, *cpumask = NULL;
#else
    nodemask_t nodeset;
#endif

    if (numa_available()==-1) {
	fprintf(stderr, "NUMA not available:");
	goto end;
    }

#ifdef HAVE_NUMA_ALLOCATE_NODEMASK
    nodemask = numa_allocate_nodemask();
    if (!nodemask) {
	fprintf(stderr, "Allocation of nodemask failed:");
	goto end;
    }

    cpumask = numa_allocate_cpumask();
    if (!cpumask) {
	fprintf(stderr, "Allocation of nodemask failed:");
	goto end;
    }
#else
    nodemask_zero(&nodeset);
#endif

    /* Try to determine the nodes */
    for (node=0; node<=numa_max_node(); node++) {
	unsigned int cpu;
#ifdef HAVE_NUMA_ALLOCATE_NODEMASK
	ret = numa_node_to_cpus(node, cpumask);
#else
	cpu_set_t CPUset;
	ret = numa_node_to_cpus(node, (unsigned long*)&CPUset, sizeof(CPUset));
#endif
	if (ret) {
	    if (errno==ERANGE) {
		fprintf(stderr, "cpumask to small for numa_node_to_cpus():");
	    } else {
		perror("numa_node_to_cpus()");
	    }
	    goto end;
	}
	for (cpu=0; cpu<CPU_SETSIZE; cpu++) {
#ifdef HAVE_NUMA_ALLOCATE_NODEMASK
	    if (CPU_ISSET(cpu, physSet)
		&& numa_bitmask_isbitset(cpumask, cpu)) {
		numa_bitmask_setbit(nodemask, node);
	    }
#else
	    if (CPU_ISSET(cpu, physSet) && CPU_ISSET(cpu, &CPUset)) {
		nodemask_set(&nodeset, node);
	    }
#endif
	}
    }
#ifdef HAVE_NUMA_ALLOCATE_NODEMASK
    numa_set_membind(nodemask);
#else
    numa_set_membind(&nodeset);
#endif

end:
#ifdef HAVE_NUMA_ALLOCATE_NODEMASK
    if (nodemask) numa_free_nodemask(nodemask);
    if (cpumask) numa_free_cpumask(cpumask);
#endif

    if (ret) fprintf(stderr, " No binding\n");
#else
    fprintf(stderr, "Daemon not build against libnuma. No binding\n");
#endif
}

void PSID_pinToCPUs(cpu_set_t *physSet)
{
    sched_setaffinity(0, sizeof(*physSet), physSet);
}

typedef struct{
    size_t maxSize;
    size_t size;
    short *map;
} CPUmap_t;

/**
 * @brief Append CPU to CPU-map
 *
 * Append the core-number @a cpu to the CPU-map @a map. If required,
 * the map's maxSize and the actual map are increased in order to make
 * to new core-number to fit into the map.
 *
 * @param cpu The core-number of the CPU to append to the map
 *
 * @param map The map to modify
 *
 * @return No return value.
 */
static void appendToMap(short cpu, CPUmap_t *map)
{
    if (map->size == map->maxSize) {
	if (map->maxSize) {
	    map->maxSize *= 2;
	} else {
	    map->maxSize = PSIDnodes_getVirtCPUs(PSC_getMyID());
	}
	map->map = realloc(map->map, map->maxSize * sizeof(*map->map));
	if (!map->map) PSID_exit(ENOMEM, "%s", __func__);
    }
    map->map[map->size] = cpu;
    map->size++;
}

/**
 * @brief Append range of CPUs to CPU-map
 *
 * Append a range of core-numbers described by the character-string @a
 * range to the CPU-map @a map.
 *
 * Range is of the form 'first[-last]' where 'first' and 'last' are
 * valid core-numbers on the local node. Be aware of the fact that the
 * result depends on the ordering of first and last. I.e. 0-3 will
 * result in 0,1,2,3 while 3-0 gives 3,2,1,0.
 *
 * @param map The map to modify
 *
 * @param range Character-string describing the range
 *
 * @return On success, 1 is returned, or 0, if an error occurred.
 */
static int appendRange(CPUmap_t *map, char *range)
{
    long first, last, i;
    char *start = strsep(&range, "-"), *end;

    if (*start == '\0') {
	fprintf(stderr, "core -%s out of range\n", range);
	return 0;
    }

    first = strtol(start, &end, 0);
    if (*end != '\0') return 0;
    if (first < 0 || first >= PSIDnodes_getVirtCPUs(PSC_getMyID())) {
	fprintf(stderr, "core %ld out of range\n", first);
	return 0;
    }

    if (range) {
	if (*range == '\0') return 0;
	last = strtol(range, &end, 0);
	if (*end != '\0') return 0;
	if (last < 0 || last >= PSIDnodes_getVirtCPUs(PSC_getMyID())) {
	    fprintf(stderr, "core %ld out of range\n", last);
	    return 0;
	}
    } else {
	last = first;
    }

    if (first > last) {
	for (i=first; i>=last; i--) appendToMap(i, map);
    } else {
	for (i=first; i<=last; i++) appendToMap(i, map);
    }

    return 1;
}

/**
 * @brief Get CPU-map from string
 *
 * Create a user-defined CPU-map @a map from the character-string @a
 * envStr. @a envStr is expected to contain a comma-separated list of
 * ranges. Each range has to be of the form 'first[,last]', where
 * 'first' and 'last' are valid (logical) core-numbers on the local
 * node.
 *
 * The array @a map is pointing to upon successful return is a static
 * member of this function. Thus, consecutive calls of this function
 * will invalidate older results.
 *
 * @param envStr The character string to parse the CPU-map from.
 *
 * @param map The parsed CPU-map.
 *
 * @return On success, the length of the parsed CPU-map is
 * returned. If an error occurred, -1 is returned.
 */
static int getMap(char *envStr, short **map)
{
    static CPUmap_t myMap = { .maxSize = 0, .size = 0, .map = NULL };
    char *range, *work = NULL, *myEnv;

    myMap.size = 0;
    *map = NULL;

    if (!envStr) {
	fprintf(stderr, "%s: missing environment\n", __func__);
	return -1;
    }

    myEnv = strdup(envStr);
    if (!myEnv) {
	fprintf(stderr, "%s: failed to handle environment\n", __func__);
	return -1;
    }

    range = strtok_r(myEnv, ",", &work);
    while (range) {
	if (!appendRange(&myMap, range)) {
	    fprintf(stderr, "%s: broken CPU-map '%s'\n", __func__, envStr);
	    free(myEnv);
	    return -1;
	}
	range = strtok_r(NULL, ",", &work);
    }

    *map = myMap.map;

    free(myEnv);

    return myMap.size;
}

cpu_set_t *PSID_mapCPUs(PSCPU_set_t set)
{
    short cpu, maxCPU = PSIDnodes_getVirtCPUs(PSC_getMyID());
    static cpu_set_t physSet;
    short *localMap = NULL;
    int localMapSize = 0;
    char *envStr = getenv("__PSI_CPUMAP");

    if (envStr && PSIDnodes_allowUserMap(PSC_getMyID())) {
	localMapSize = getMap(envStr, &localMap);
	if (localMapSize < 0) {
	    fprintf(stderr, "%s: falling back to system default\n", __func__);
	    localMapSize = 0;
	}
    }

    CPU_ZERO(&physSet);
    for (cpu=0; cpu<maxCPU; cpu++) {
	if (PSCPU_isSet(set, cpu)) {
	    short physCPU = -1;
	    if (localMapSize) {
		if (cpu < localMapSize) physCPU = localMap[cpu];
	    } else {
		physCPU = PSIDnodes_mapCPU(PSC_getMyID(), cpu);
	    }
	    if (physCPU<0 || physCPU >= maxCPU) {
		fprintf(stderr,
			"Mapping CPU %d->%d out of range. No pinning\n",
			cpu, physCPU);
		continue;
	    }
	    CPU_SET(physCPU, &physSet);
	}
    }

    {
	char txt[PSCPU_MAX+2] = { '\0' };
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
 * @brief Setup RT-priority
 *
 * Setup realtime-priorities for the client process.
 *
 * This is a temporary fix for St. Graf (s.graf@fz-juelich.de).
 *
 * @return No return value
 */
static void adaptPriority(void)
{
    char *prioStr;
    int policy;

    if ((prioStr = getenv("__SCHED_FIFO"))) {
	policy = SCHED_FIFO;
    } else if ((prioStr = getenv("__SCHED_RR"))) {
	policy = SCHED_RR;
    }
    if (prioStr) {
	char *end;
	int priority = strtol(prioStr, &end, 0);

	if (end && !*end) {
	    struct sched_param params;

	    params.sched_priority = priority;
	    sched_setscheduler(0, policy, &params);
	} else {
	    fprintf(stderr, "%s: unknown priority '%s'\n", __func__, prioStr);
	}
    }
}

/**
 * @brief Change into working directory
 *
 * Try to change into the client-task's @a task working directory. If
 * this fails, changing into the corresponding user's home-directory
 * is attempted.
 *
 * @param task Structure describing the client-task.
 *
 * @return Upon success 0 is returned. If an error occurred, an
 * error-code (i.e. an errno) different from 0 is returned.
 */
static int changeToWorkDir(PStask_t *task)
{
    char *rawIO = getenv("__PSI_RAW_IO");
    alarmFunc = __func__;

    if (chdir(task->workingdir)<0) {
	struct passwd *passwd;

	if (!rawIO) {
	    fprintf(stderr, "%s: chdir(%s): %s\n", __func__,
		    task->workingdir ? task->workingdir : "",
		    get_strerror(errno));
	    fprintf(stderr, "Will use user's home directory\n");
	}

	passwd = getpwuid(getuid());
	if (passwd) {
	    if (chdir(passwd->pw_dir)<0) {
		int eno = errno;
		if (rawIO) {
		    PSID_warn(-1, eno, "%s: chdir(%s)", __func__,
			      passwd->pw_dir ? passwd->pw_dir : "");
		} else {
		    fprintf(stderr, "%s: chdir(%s): %s\n", __func__,
			    passwd->pw_dir ? passwd->pw_dir : "",
			    get_strerror(eno));
		}
		return eno;
	    }
	} else {
	    if (rawIO) {
		PSID_log(-1, "Cannot determine home directory\n");
	    } else {
		fprintf(stderr, "Cannot determine home directory\n");
	    }
	    return ENOENT;
	}
    }

    return 0;
}

/**
 * @brief Do various process clamps.
 *
 * Pin process to the logical CPU-set @a set and bind it to the NUMA
 * nodes serving these logical CPUs if demanded on the local
 * node. Therefore @ref PSID_pinToCPU() and @ref PSID_bindToNode() are
 * called respectively.
 *
 * Before doing the actual pinning and binding the logical CPUs are
 * mapped to physical ones via PSID_mapCPUs().
 *
 * @param set The logical CPUs to pin and bind to.
 *
 * @return No return value.
 *
 * @see PSID_bindToNode(), PSID_pinToCPU(), PSID_mapCPUs()
 */
static void doClamps(PStask_t *task)
{
    setenv("PSID_CPU_PINNING",  PSCPU_print(task->CPUset), 1);

    int16_t physCPUs = PSIDnodes_getVirtCPUs(PSC_getMyID());

    if (!PSCPU_any(task->CPUset, physCPUs)) {
	fprintf(stderr, "CPU slots not set. Old executable? "
		"You might want to relink your program.\n");
    } else if (PSCPU_all(task->CPUset, physCPUs)) {
	/* No mapping */
    } else if (PSIDnodes_pinProcs(PSC_getMyID())
	       || PSIDnodes_bindMem(PSC_getMyID())) {
#ifdef CPU_ZERO
	cpu_set_t *physSet = PSID_mapCPUs(task->CPUset);

	if (PSIDnodes_pinProcs(PSC_getMyID())) {
	    if (getenv("__PSI_NO_PINPROC")) {
		fprintf(stderr, "Pinning suppressed for rank %d\n", task->rank);
	    } else {
		PSID_pinToCPUs(physSet);
	    }
	}
	if (PSIDnodes_bindMem(PSC_getMyID())) {
	    if (getenv("__PSI_NO_MEMBIND")) {
		fprintf(stderr, "Binding suppressed for rank %d\n", task->rank);
	    } else {
		PSID_bindToNodes(physSet);
	    }
	}
#else
	fprintf(stderr, "Daemon has no sched_setaffinity(). No pinning\n");
#endif
    }
}

/**
 * @brief Test, if tasks executable is there
 *
 * Test, if the child-task's @a task executable is available. If the
 * task's argv[0] contains an absolute path, only this is
 * search. Otherwise all paths listed in the PATH environment
 * variables are searched for the executable.
 *
 * If the executable is found, it's accessibility is tested.
 *
 * If all tests are passed, @a executable is set accordingly. Future
 * calls to execv() might get @a executable as the first argument.
 *
 * @param task Structure describing the client-task.
 *
 * @param executable The actual executable identified. This might be
 * passed to execv().
 *
 * @return Upon success 0 is returned. If an error occurred, an
 * error-code (i.e. an errno) different from 0 is returned.
 */
static int testExecutable(PStask_t *task, char **executable)
{
    struct stat sb;
    int execFound = 0, fd, ret;
    char buf[64];

    alarmFunc = __func__;
    if (!task->argv[0]) {
	fprintf(stderr, "No argv[0] given!\n");
	return ENOENT;
    }

    if (!strcmp(task->argv[0], "$SHELL")) {
	struct passwd *passwd = getpwuid(getuid());
	if (!passwd) {
	    int eno = errno;
	    fprintf(stderr, "%s: Unable to determine $SHELL: %s\n", __func__,
		    get_strerror(eno));
	    return eno;
	}
	free(task->argv[0]);
	task->argv[0] = strdup(passwd->pw_shell);
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
	    int eno = errno;
	    fprintf(stderr, "%s: stat(%s): %s\n", __func__,
		    task->argv[0] ? task->argv[0] : "", get_strerror(eno));
	    return eno;
	}
    }

    if (!S_ISREG(sb.st_mode) || !(sb.st_mode & S_IXUSR)) {
	fprintf(stderr, "%s: stat(): %s\n", __func__,
		(!S_ISREG(sb.st_mode)) ? "S_ISREG error" :
		(sb.st_mode & S_IXUSR) ? "" : "S_IXUSR error");
	return EACCES;
    }

    /* Try to read first 64 bytes; on Lustre this might hang */
    if ((fd = open(task->argv[0],O_RDONLY)) < 0) {
	int eno = errno;
	if (eno != EACCES) { /* Might not have to permission to read (#1058) */
	    fprintf(stderr, "%s: open(): %s\n", __func__, get_strerror(eno));
	    return eno;
	}
    } else if ((ret = read(fd, buf, sizeof(buf))) < 0) {
	int eno = errno;
	fprintf(stderr, "%s: read(): %s\n", __func__, get_strerror(eno));
	return eno;
    } else {
	close(fd);
    }

    *executable = task->argv[0];

    /* shells */
    if (!strcmp(*executable, "/bin/bash")) {
	if (task->argc == 2 && !strcmp(task->argv[1], "-i")) {
	    task->argv[0] = "-bash";
	    task->argv[1] = NULL;
	    task->argc = 1;
	} else {
	    task->argv[0] = "bash";
	}
    } else if (!strcmp(*executable, "/bin/tcsh")) {
	if (task->argc == 2 && !strcmp(task->argv[1], "-i")) {
	    task->argv[0] = "-tcsh";
	    task->argv[1] = NULL;
	    task->argc = 1;
	} else {
	    task->argv[0] = "tcsh";
	}
    }

    return 0;
}

static void restoreLimits(void)
{
    int i;

    for (i=0; PSP_rlimitEnv[i].envName; i++) {
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
 * and accessibility of the executable to call the forwarder is
 * signaled via the control-channel passwd withing task->fd to
 * register the child process within the local daemon. As soon as the
 * forwarder acknowledges the registration finally the executable is
 * called via @ref myexecv().
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
    int ret, eno = 0, timeout = 30;
    char *executable = NULL, *envStr;

    /* change the gid */
    if (setgid(task->gid)<0) {
	eno = errno;
	fprintf(stderr, "%s: setgid: %s\n", __func__, get_strerror(eno));
	ret = write(task->fd, &eno, sizeof(eno));
	if (ret < 0) {
	    eno = errno;
	    fprintf(stderr, "%s: setgid: write(): %s\n", __func__,
		    get_strerror(eno));
	}
	exit(1);
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

    /* This is a temporary fix for St. Graf (s.graf@fz-juelich.de). */
    /* It requires root permissions */
    adaptPriority();

    /* change the uid */
    if (setuid(task->uid)<0) {
	eno = errno;
	fprintf(stderr, "%s: setuid: %s\n", __func__, get_strerror(eno));
	if (write(task->fd, &eno, sizeof(eno)) < 0) {
	    eno = errno;
	    fprintf(stderr, "%s: setuid: write(): %s\n", __func__,
		    get_strerror(eno));
	}
	exit(1);
    }

    /* restore various resource limits */
    restoreLimits();

    /* restore umask settings */
    envStr = getenv("__PSI_UMASK");
    if (envStr) {
	mode_t mask;
	int ret = sscanf(envStr, "%o", &mask);
	if (ret > 0) umask(mask);
    }

    alarmFD = task->fd;
    signal(SIGALRM, alarmHandler);
    envStr = getenv("__PSI_ALARM_TMOUT");
    if (envStr) {
	int tmout, ret = sscanf(envStr, "%d", &tmout);
	if (ret > 0) timeout = tmout;
    }
    alarm(timeout);
    if ((eno = changeToWorkDir(task))) {
	if (write(task->fd, &eno, sizeof(eno)) < 0) {
	    eno = errno;
	    fprintf(stderr, "%s: changeToWorkDir: write(): %s\n", __func__,
		    get_strerror(eno));
	}
	exit(1);
    }

    if ((eno = testExecutable(task, &executable))) {
	if (write(task->fd, &eno, sizeof(eno)) < 0) {
	    eno = errno;
	    fprintf(stderr, "%s: testExecutable: write(): %s\n", __func__,
		    get_strerror(eno));
	}
	exit(1);
    }
    alarm(0);

    doClamps(task);

    /* Signal forwarder we're ready for execve() */
    if (write(task->fd, &eno, sizeof(eno)) < 0) {
	eno = errno;
	fprintf(stderr, "%s: write(): %s\n", __func__, get_strerror(eno));
	PSID_exit(eno, "%s: write()", __func__);
    }

    if (read(task->fd, &eno, sizeof(eno)) < 0) {
	eno = errno;
	fprintf(stderr, "%s: read(): %s\n", __func__, get_strerror(eno));
	PSID_exit(eno, "%s: read()", __func__);
    }

    if (eno) {
	fprintf(stderr, "%s: DD_CHILDBORN failed\n", __func__);
	PSID_log(-1, "%s: DD_CHILDBORN failed\n", __func__);
	exit(1);
    }

    /* execute the image */
    if (myexecv(executable, task->argv) < 0) {
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
	(fileNo == STDERR_FILENO) ? "stderr" : "unknown";

    if (task->aretty & (1<<fileNo)) {
	if (openpty(&fds[0], &fds[1], NULL, &task->termios, &task->winsize)) {
	    int eno = errno;
	    PSID_warn(-1, errno, "%s: openpty(%s)", __func__, fdName);
	    return eno;
	}
    } else {
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, fds)) {
	    int eno = errno;
	    PSID_warn(-1, errno, "%s: socketpair(%s)", __func__, fdName);
	    return eno;
	}
    }

    return 0;
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
static void execForwarder(PStask_t *task, int daemonfd)
{
    pid_t pid;
    int stdinfds[2], stdoutfds[2], stderrfds[2] = {-1, -1}, controlfds[2];
    int ret, eno = 0;
    char *envStr;
    struct timeval start, end = { .tv_sec = 0, .tv_usec = 0 }, stv;
    struct timeval timeout = { .tv_sec = 30, .tv_usec = 0};

    /* Block until the forwarder has handled all output */
    PSID_blockSig(1, SIGCHLD);

    /* setup the environment; done here to pass it to forwarder, too */
    setenv("PWD", task->workingdir, 1);

    if (task->environ) {
	int i;
	for (i=0; task->environ[i]; i++) {
	    putenv(strdup(task->environ[i]));
	}
    }

    /* create a socketpair for communication between forwarder and client */
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, controlfds)<0) {
	eno = errno;
	PSID_warn(-1, eno, "%s: socketpair()", __func__);
	goto error;
    }

    if (task->aretty & (1<<STDIN_FILENO)
	&& task->aretty & (1<<STDOUT_FILENO)
	&& task->aretty & (1<<STDERR_FILENO)) task->interactive = 1;

    /* create stdin/stdout/stderr connections between forwarder & client */
    if (task->interactive) {
	if ((eno = openChannel(task, stderrfds, STDERR_FILENO))) goto error;
    } else {
	/* first stdout */
	if ((eno = openChannel(task, stdoutfds, STDOUT_FILENO))) goto error;

	/* then stderr */
	if ((eno = openChannel(task, stderrfds, STDERR_FILENO))) goto error;

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
		    eno = errno;
		    PSID_warn(-1, eno, "%s: openpty(stdin)", __func__);
		    goto error;
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
		    eno = errno;
		    PSID_warn(-1, eno, "%s: socketpair(stdin)", __func__);
		    goto error;
		}
	    }
	}
    }

    /* init the process manager sockets */
    if ((PSIDhook_call(PSIDHOOK_EXEC_FORWARDER, task)) == -1) {
	eno = EINVAL;
	goto error;
    }

    /* fork the client */
    if (!(pid = fork())) {
	/* this is the client process */
	/* no direct connection to the daemon */
	close(daemonfd);

	/* prepare connection to forwarder */
	task->fd = controlfds[1];
	close(controlfds[0]);

	/* Reset connection to syslog */
	closelog();
	openlog("psid_client", LOG_PID|LOG_CONS, config->logDest);

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
	    eno = errno;
	    if (write(task->fd, &eno, sizeof(eno)) < 0) {
		PSID_warn(-1, errno, "%s: dup2(stderr): write()", __func__);
	    }
	    PSID_exit(eno, "%s: dup2(stderr)", __func__);
	}

	/* From now on, all logging is done via the forwarder thru stderr */

	if (dup2(task->stdin_fd, STDIN_FILENO) < 0) {
	    eno = errno;
	    fprintf(stderr, "%s: dup2(stdin): [%d] %s\n", __func__,
		    eno, get_strerror(eno));
	    if (write(task->fd, &eno, sizeof(eno)) < 0) {
		PSID_warn(-1, errno, "%s: dup2(stdin): write()", __func__);
	    }
	    PSID_exit(eno, "%s: dup2(stdin)", __func__);
	}
	if (dup2(task->stdout_fd, STDOUT_FILENO) < 0) {
	    eno = errno;
	    fprintf(stderr, "%s: dup2(stdout): [%d] %s\n", __func__,
		    eno, get_strerror(eno));
	    if (write(task->fd, &eno, sizeof(eno)) < 0) {
		PSID_warn(-1, errno, "%s: dup2(stdout): write()", __func__);
	    }
	    PSID_exit(eno, "%s: dup2(stdout)", __func__);
	}

	/* close the now useless slave ttys / sockets */
	close(task->stderr_fd);
	if (!task->interactive) {
	    close(task->stdin_fd);
	    close(task->stdout_fd);
	}

	/* close forwarder socket */
	PSIDhook_call(PSIDHOOK_EXEC_CLIENT, task);

	/* try to start the client */
	execClient(task);

	/* Never be here */
	exit(0);
    }

    /* this is the forwarder process */

    /* save errno in case of error */
    if (pid == -1) eno = errno;

    /* prepare connection to child */
    task->fd = controlfds[0];
    close(controlfds[1]);

    /* close the slave ttys / sockets */
    if (task->interactive) {
	close(stderrfds[1]);
	task->stdin_fd = stderrfds[0];
	task->stdout_fd = stderrfds[0];
	task->stderr_fd = stderrfds[0]; /* req. by SPAWNFAILED extension */
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
	PSID_warn(-1, eno, "%s: fork()", __func__);
	goto error;
    }

    /* change the gid */
    if (setgid(task->gid)<0) {
	eno = errno;
	PSID_warn(-1, eno, "%s: setgid()", __func__);
	goto error;
    }
    /* change the uid */
    if (setuid(task->uid)<0) {
	eno = errno;
	PSID_warn(-1, eno, "%s: setuid()", __func__);
	goto error;
    }

    /* check for a sign from the client */
    PSID_log(PSID_LOG_SPAWN, "%s: waiting for my child (%d)\n", __func__, pid);

    /* Pass the client's PID to the forwarder. */
    task->tid = PSC_getTID(-1, pid);

    /* Just wait a finite time for the client process */
    envStr = getenv("__PSI_ALARM_TMOUT");
    if (envStr) {
	int tmout, ret = sscanf(envStr, "%d", &tmout);
	if (ret > 0) timeout.tv_sec = tmout;
    }
    timeout.tv_sec += 2;  /* 2 secs more than client */

    gettimeofday(&start, NULL);                   /* get starttime */
    timeradd(&start, &timeout, &end);             /* add given timeout */

    do {
	fd_set rfds;

	FD_ZERO(&rfds);
	FD_SET(task->fd, &rfds);

	gettimeofday(&start, NULL);               /* get NEW starttime */
	timersub(&end, &start, &stv);
	if (stv.tv_sec < 0) timerclear(&stv);

	ret = select(task->fd+1, &rfds, NULL, NULL, &stv);
	if (ret == -1) {
	    if (errno == EINTR) {
		/* Interrupted syscall, just start again */
		const struct timeval delta = { .tv_sec = 0, .tv_usec = 10 };
		timersub(&end, &delta, &start);       /* assure next round */
		continue;
	    } else {
		eno = errno;
		PSID_warn(-1, eno, "%s: select() failed", __func__);
		break;
	    }
	} else if (!ret) {
	    PSID_log(-1, "%s: select() timed out\n", __func__);
	    eno = ETIME;
	    break;
	} else {
	    break;
	}
	gettimeofday(&start, NULL);  /* get NEW starttime */
    } while (timercmp(&start, &end, <));

    if (eno) goto error;

restart:
    if ((ret=read(task->fd, &eno, sizeof(eno))) < 0) {
	if (errno == EINTR) {
	    goto restart;
	}
	eno = errno;
	PSID_warn(-1, eno, "%s: read() failed. eno is %d", __func__, eno);
	goto error;
    }

    if (!ret) {
	PSID_log(-1, "%s: ret is %d\n", __func__, ret);
	eno = EBADMSG;
    }

error:
    /* Release the waiting daemon and exec forwarder */
    PSID_forwarder(task, daemonfd, eno);

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
 * All necessary information determined during start up of the
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
static int buildSandboxAndStart(PStask_t *task)
{
    int socketfds[2];     /* sockets for communication with forwarder */
    pid_t pid;            /* forwarder's pid */
    int i, eno, blocked;

    if (PSID_getDebugMask() & PSID_LOG_SPAWN) {
	char tasktxt[128];
	PStask_snprintf(tasktxt, sizeof(tasktxt), task);
	PSID_log(PSID_LOG_TASK, "%s: task=%s\n", __func__, tasktxt);
    }

    /* create a socketpair for communication between daemon and forwarder */
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, socketfds)<0) {
	eno = errno;
	PSID_warn(-1, eno, "%s: socketpair()", __func__);
	return eno;
    }
    if (socketfds[0] >= FD_SETSIZE) {
	PSID_log(-1, "%s: socketfd (%d) out of mask\n", __func__,
		 socketfds[0]);
	close(socketfds[0]);
	close(socketfds[1]);

	return EOVERFLOW;
    }

    /* fork the forwarder */
    blocked = PSID_blockSig(1, SIGCHLD);
    pid = fork();
    /* save errno in case of error */
    eno = errno;

    if (!pid) {
	/* this is the forwarder process */

	PSID_resetSigs();
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
	for (i=0; i<getdtablesize(); i++) {
	    if (i!=STDIN_FILENO && i!=STDOUT_FILENO && i!=STDERR_FILENO
		&& i!=socketfds[1]) {
		close(i);
	    }
	}
	/* Reopen the syslog and rename the tag */
	openlog("psidforwarder", LOG_PID|LOG_CONS, config->logDest);

	/* Get rid of now useless selectors */
	Selector_init(NULL);
	/* Get rid of obsolete timers */
	Timer_init(NULL);

	PSC_setDaemonFlag(0);
	PSC_resetMyTID();

	execForwarder(task, socketfds[1]);
    }
    PSID_blockSig(blocked, SIGCHLD);

    /* this is the parent process */

    /* close forwarders end of the socketpair */
    close(socketfds[1]);

    /* check if fork() was successful */
    if (pid == -1) {
	close(socketfds[0]);

	PSID_warn(-1, eno, "%s: fork()", __func__);

	return eno;
    }

    task->tid = PSC_getTID(-1, pid);
    task->fd = socketfds[0];
    /* check for a sign of the forwarder */
    PSID_log(PSID_LOG_SPAWN, "%s: waiting for my child (%d)\n", __func__, pid);

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
    DDTypedBufferMsg_t msg;
    char *ptr = msg.buf;
    unsigned short maxCPUs = 0;
    int slotsChunk, slot, num = task->partitionSize;
    size_t nBytes;

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

    /* total number of children */
    *(int32_t *)ptr = num;
    ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    sendMsg((DDMsg_t *)&msg);

    msg.type = PSP_ACCOUNT_SLOTS;

    for (slot = 0; slot < num; slot++) {
	unsigned short cpus = PSIDnodes_getVirtCPUs(task->partition[slot].node);
	if (cpus > maxCPUs) maxCPUs = cpus;
    }

    if (!num || !maxCPUs) {
	PSID_log(-1, "%s: No CPUs\n", __func__);
	return;
    }

    nBytes = PSCPU_bytesForCPUs(maxCPUs);
    slotsChunk = 1024 / (sizeof(PSnodes_ID_t) + nBytes);

    for (slot = 0; slot < num; slot++) {
	if (! (slot%slotsChunk)) {
	    if (slot) sendMsg((DDMsg_t *)&msg);

	    msg.header.len = sizeof(msg.header) + sizeof(msg.type);

	    /* skip logger's TID */
	    ptr = msg.buf + sizeof(PStask_ID_t);
	    msg.header.len += sizeof(PStask_ID_t);

	    /* child's uid */
	    *(uid_t *)ptr = task->uid;
	    ptr += sizeof(uid_t);
	    msg.header.len += sizeof(uid_t);

	    /* number of slots to add now */
	    *(uint16_t *)ptr = (num-slot < slotsChunk) ? num-slot : slotsChunk;
	    ptr += sizeof(uint16_t);
	    msg.header.len += sizeof(uint16_t);

	    /* size of CPUset part */
	    *(uint16_t *)ptr = nBytes;
	    ptr += sizeof(uint16_t);
	    msg.header.len += sizeof(uint16_t);
	}

	*(PSnodes_ID_t *)ptr = task->partition[slot].node;
	ptr += sizeof(PSnodes_ID_t);
	msg.header.len += sizeof(PSnodes_ID_t);

	PSCPU_extract(ptr, task->partition[slot].CPUset, nBytes);
	ptr += nBytes;
	msg.header.len += nBytes;
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
	PSID_log(-1, "%s: sending task not found\n", __func__);
	return EACCES;
    }

    if (sender != task->ptid && stask->group != TG_FORWARDER) {
	/* Sender has to be parent or a trusted forwarder */
	PSID_log(-1, "%s: spawner tries to cheat\n", __func__);
	return EACCES;
    }

    ptask = PStasklist_find(&managedTasks, task->ptid);
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

    if ((task->group == TG_SERVICE || task->group == TG_SERVICE_SIG
	|| task->group == TG_KVS) && task->rank >= -1) {
	/* wrong rank for service task */
	PSID_log(-1, "%s: rank %d for service task\n", __func__, task->rank);
	return EINVAL;
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
    int err;

    if (!task) return EINVAL;

    /* now try to start the task */
    err = buildSandboxAndStart(task);

    if (!err) {
	/* prepare forwarder task */
	task->childGroup = task->group;
	task->group = TG_FORWARDER;
	task->protocolVersion = PSProtocolVersion;
	/* Enqueue the forwarder */
	PStasklist_enqueue(&managedTasks, task);
	/* The forwarder is already connected and established */
	registerClient(task->fd, task->tid, task);
	setEstablishedClient(task->fd);
	/* Tell everybody about the new forwarder task */
	incJobs(1, 0);
    } else {
	PSID_warn(PSID_LOG_SPAWN, err, "%s: buildSandboxAndStart()", __func__);

	PStask_delete(task);
    }

    return err;
}

/**
 * @brief Handle a PSP_CD_SPAWNREQUEST message.
 *
 * Handle the message @a msg of type PSP_CD_SPAWNREQUEST. These are
 * replaced by PSP_CD_SPAWNREQ messages in later versions of the
 * protocol. For reasons of compatibility the old requests are still
 * supported.
 *
 * Spawn a process as described with @a msg. Therefor a @ref PStask_t
 * structure is extracted from @a msg. If called on the node of the
 * initiating task, various tests are undertaken in order to determine
 * the spawn to be allowed. If all tests pass, the message is
 * forwarded to the target-node where the process to spawn is created.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_SPAWNREQUEST(DDBufferMsg_t *msg)
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
    answer.request = task->rank;

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

	if (answer.error) {
	    /* send only on failure. success reported by forwarder */
	    answer.header.type = PSP_CD_SPAWNFAILED;
	    sendMsg(&answer);
	}
    } else {
	/* request for a remote site. */
	PStask_delete(task);

	if (!PSIDnodes_isUp(PSC_getID(msg->header.dest))) {
	    answer.header.type = PSP_CD_SPAWNFAILED;
	    answer.header.sender = msg->header.dest;
	    answer.error = EHOSTDOWN;
	    sendMsg(&answer);

	    return;
	}

	PSID_log(PSID_LOG_SPAWN, "%s: forwarding to node %d\n",
		 __func__, PSC_getID(msg->header.dest));

	if (sendMsg(msg) < 0) {
	    answer.header.type = PSP_CD_SPAWNFAILED;
	    answer.header.sender = msg->header.dest;
	    answer.error = errno;

	    sendMsg(&answer);
	}

    }
}

/**
 * List of all tasks waiting to get spawned, i.e. waiting for last
 * environment packets to come in.
 */
static LIST_HEAD(spawnTasks);

/**
 * @brief Handle a PSP_CD_SPAWNREQ message.
 *
 * Handle the message @a msg of type PSP_CD_SPAWNREQ. These replace
 * the PSP_CD_SPAWNREQUEST messages of earlier protocol versions.
 *
 * Spawn a process as described within a series of messages. Depending
 * on the subtype of the current message @a msg, either the @ref
 * PStask_t structure contained is extracted or the argv or
 * environment parts are decoded and added to the corresponding task
 * structure. After receiving the last part of the environment the
 * actual task is created.
 *
 * If called on the node of the initiating task, various
 * tests are undertaken in order to determine the spawn to be
 * allowed. If all tests pass, the message is forwarded to the
 * target-node where the process to spawn is created.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_SPAWNREQ(DDTypedBufferMsg_t *msg)
{
    PStask_t *task, *ptask = NULL;
    DDErrorMsg_t answer = {
	.header = {
	    .type = PSP_CD_SPAWNFAILED,
	    .sender = msg->header.dest,
	    .dest = msg->header.sender,
	    .len = sizeof(answer) },
	.error = 0,
	.request = 0,};
    size_t usedBytes;
    int32_t rank = -1;
    PStask_group_t group = TG_ANY;

    char tasktxt[128];

    PSID_log(PSID_LOG_SPAWN, "%s: from %s msglen %d\n", __func__,
	     PSC_printTID(msg->header.sender), msg->header.len);

    /* If message is from my node, test if everything is okay */
    if (PSC_getID(msg->header.sender)==PSC_getMyID()
	&& msg->type == PSP_SPAWN_TASK) {
	task = PStask_new();
	PStask_decodeTask(msg->buf, task);
	answer.request = task->rank;
	answer.error = checkRequest(msg->header.sender, task);

	if (answer.error) {
	    PStask_delete(task);
	    sendMsg(&answer);

	    return;
	}
	/* Store some info from task for latter usage */
	group = task->group;
	rank = task->rank;

	PStask_delete(task);

	/* Since checkRequest() did not fail, we will find ptask */
	ptask = PStasklist_find(&managedTasks, msg->header.sender);
	if (!ptask) {
	    PSID_log(-1, "%s: no parent task?!\n", __func__);
	    return;
	}
    }


    if (PSC_getID(msg->header.dest) != PSC_getMyID()) {
	/* request for a remote site. */
	int destDmnPSPver = PSIDnodes_getDmnProtoV(PSC_getID(msg->header.dest));
	int sendLOC = 0;

	if (!PSIDnodes_isUp(PSC_getID(msg->header.dest))) {
	    answer.error = EHOSTDOWN;
	    sendMsg(&answer);

	    return;
	}

	/* Check if we have to and can send a LOC-message */
	if (PSC_getID(msg->header.sender)==PSC_getMyID()
	    && msg->type == PSP_SPAWN_TASK && group != TG_SERVICE
	    && group != TG_SERVICE_SIG && group != TG_ADMINTASK
	    && group != TG_KVS) {

	    if (!ptask->spawnNodes || rank >= ptask->spawnNum) {
		PSID_log(-1, "%s: rank %d out of range\n", __func__, rank);
		answer.error = EADDRNOTAVAIL;
	    } else if (!PSCPU_any(ptask->spawnNodes[rank].CPUset, PSCPU_MAX)) {
		PSID_log(-1, "%s: rank %d exhausted\n", __func__, rank);
		answer.error = EADDRINUSE;
	    } else {
		sendLOC = 1;
	    }

	    if (!sendLOC) {
		sendMsg(&answer);

		return;
	    }
	}

	PSID_log(PSID_LOG_SPAWN, "%s: forwarding to node %d\n",
		 __func__, PSC_getID(msg->header.dest));
	if (sendMsg(msg) < 0) {
	    answer.error = errno;
	    sendMsg(&answer);

	    return;
	}

	if (sendLOC) {
	    /* Create and send PSP_SPAWN_LOC message */
	    DDTypedBufferMsg_t locMsg = (DDTypedBufferMsg_t) {
		.header = (DDMsg_t) {
		    .type = PSP_CD_SPAWNREQ,
		    .dest = msg->header.dest,
		    .sender = msg->header.sender,
		    .len = sizeof(locMsg.header) + sizeof(locMsg.type)},
		.type = PSP_SPAWN_LOC };
	    PSCPU_set_t *rankSet = &ptask->spawnNodes[rank].CPUset;
	    PSnodes_ID_t destID = PSC_getID(locMsg.header.dest);

	    if (destDmnPSPver < 401) {
		short numCPUs = PSIDnodes_getPhysCPUs(destID);
		int16_t cpu = PSCPU_first(*rankSet, numCPUs);
		PSP_putTypedMsgBuf(&locMsg, __func__, "cpu", &cpu, sizeof(cpu));
	    } else if (destDmnPSPver < 408) {
		size_t nBytes = PSCPU_bytesForCPUs(32);
		PSCPU_set_t setBuf;
		PSCPU_extract(setBuf, *rankSet, nBytes);
		PSP_putTypedMsgBuf(&locMsg, __func__, "CPUset", setBuf, nBytes);
	    } else {
		PSCPU_set_t setBuf;
		short numCPUs = PSIDnodes_getVirtCPUs(destID);
		uint16_t nBytes = PSCPU_bytesForCPUs(numCPUs);

		PSP_putTypedMsgBuf(&locMsg, __func__, "nBytes", &nBytes,
				   sizeof(nBytes));

		PSCPU_extract(setBuf, *rankSet, nBytes);
		PSP_putTypedMsgBuf(&locMsg, __func__, "CPUset", setBuf, nBytes);

		/* Invalidate this entry */
		PSCPU_clrAll(*rankSet);
	    }

	    PSID_log(PSID_LOG_SPAWN, "%s: send PSP_SPAWN_LOC to node %d\n",
		     __func__, PSC_getID(locMsg.header.dest));

	    if (sendMsg(&locMsg) < 0) {
		PSID_warn(-1, errno, "%s: send PSP_SPAWN_LOC to node %d failed",
			  __func__, PSC_getID(locMsg.header.dest));
	    }
	}

	return;
    }

    task = PStasklist_find(&spawnTasks, msg->header.sender);
    if (task) answer.request = task->rank;

    switch (msg->type) {
    case PSP_SPAWN_TASK:
	if (task) {
	    PStask_snprintf(tasktxt, sizeof(tasktxt), task);
	    PSID_log(-1, "%s: from %s task %s already there\n",
		     __func__, PSC_printTID(msg->header.sender), tasktxt);
	    return;
	}
	task = PStask_new();
	PStask_decodeTask(msg->buf, task);
	task->tid = msg->header.sender;
	if (PSIDnodes_getProtoV(PSC_getID(msg->header.sender)) > 339) {
	    task->argc = 0;           /* determine from argv later */
	}

	/* Check if we have to and can copy the location */
	if (task->group == TG_SERVICE || task->group == TG_SERVICE_SIG
	    || task->group == TG_ADMINTASK || task->group == TG_KVS) {
	    PSCPU_setAll(task->CPUset);
	} else if (PSC_getID(msg->header.sender)==PSC_getMyID()) {
	    if (!ptask->spawnNodes || rank >= ptask->spawnNum) {
		PSID_log(-1, "%s: rank %d out of range\n", __func__, rank);
		answer.error = EADDRNOTAVAIL;
	    } else if (!PSCPU_any(ptask->spawnNodes[rank].CPUset, PSCPU_MAX)) {
		PSID_log(-1, "%s: rank %d exhausted\n", __func__, rank);
		answer.error = EADDRINUSE;
	    } else {
		PSCPU_set_t *rankSet = &ptask->spawnNodes[rank].CPUset;
		memcpy(task->CPUset, *rankSet, sizeof(task->CPUset));

		/* Invalidate this entry */
		PSCPU_clrAll(*rankSet);
	    }

	    if  (answer.error) {
		sendMsg(&answer);

		PStask_delete(task);

		return;
	    }
	}

	PStask_snprintf(tasktxt, sizeof(tasktxt), task);
	PSID_log(PSID_LOG_SPAWN, "%s: create %s\n", __func__, tasktxt);

	PStasklist_enqueue(&spawnTasks, task);

	return;
	break;
    case PSP_SPAWN_LOC:
    {
	int srcDmnPSPver =
	    PSIDnodes_getDmnProtoV(PSC_getID(msg->header.sender));

	if (!task) {
	    PSID_log(-1, "%s: PSP_SPAWN_LOC from %s: task not found\n",
		     __func__, PSC_printTID(msg->header.sender));
	    return;
	}
	if (srcDmnPSPver < 401) {
	    PSCPU_setCPU(task->CPUset, *(int16_t *)msg->buf);
	    usedBytes = sizeof(int16_t);
	} else {
	    size_t nBytes, myBytes = PSCPU_bytesForCPUs(PSCPU_MAX);
	    char *ptr = msg->buf;

	    if (srcDmnPSPver < 408) {
		nBytes = PSCPU_bytesForCPUs(32);
		usedBytes = 0;
	    } else {
		nBytes = *(uint16_t *)ptr;
		ptr += sizeof(uint16_t);
		usedBytes = sizeof(uint16_t);
	    }

	    if (nBytes > myBytes) {
		PSID_log(-1,  "%s: PSP_SPAWN_LOC from %s: expecting %zd CPUs\n",
			 __func__, PSC_printTID(msg->header.sender), nBytes*8);
	    }

	    PSCPU_clrAll(task->CPUset);
	    PSCPU_inject(task->CPUset, ptr, nBytes);
	    // ptr += nBytes;
	    usedBytes += nBytes;
	}
	break;
    }
    case PSP_SPAWN_WDIRCNTD:
	if (!task) {
	    PSID_log(-1, "%s: PSP_SPAWN_WDIRCNTD from %s: task not found\n",
		     __func__, PSC_printTID(msg->header.sender));
	    return;
	}
	{
	    size_t newLen = task->workingdir ? strlen(task->workingdir) : 0;
	    newLen += strlen(msg->buf) + 1;

	    task->workingdir = realloc(task->workingdir, newLen);
	    if (!task->workingdir) {
		PSID_warn(-1, errno, "%s: realloc(task->workingdir)", __func__);
		return;
	    }
	    strcpy(task->workingdir + strlen(task->workingdir), msg->buf);

	    usedBytes = strlen(msg->buf) + 1;
	}
	break;
    case PSP_SPAWN_ARG:
	if (!task) {
	    PSID_log(-1, "%s: PSP_SPAWN_ARG from %s: task not found\n",
		     __func__, PSC_printTID(msg->header.sender));
	    return;
	}
	if (PSIDnodes_getProtoV(PSC_getID(msg->header.sender)) < 340) {
	    usedBytes = PStask_decodeArgs(msg->buf, task);
	} else {
	    usedBytes = PStask_decodeArgv(msg->buf, task);
	}
	break;
    case PSP_SPAWN_ARGCNTD:
	if (!task) {
	    PSID_log(-1, "%s: PSP_SPAWN_ARGCTND from %s: task not found\n",
		     __func__, PSC_printTID(msg->header.sender));
	    return;
	}
	usedBytes = PStask_decodeArgvAppend(msg->buf, task);
	break;
    case PSP_SPAWN_ENV:
    case PSP_SPAWN_END:
	if (!task) {
	    PSID_log(-1, "%s: PSP_SPAWN_EN[V|D] from %s: task not found\n",
		     __func__, PSC_printTID(msg->header.sender));
	    if (msg->type == PSP_SPAWN_END) {
		answer.error = ECHILD;
		sendMsg(&answer);
	    }
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

	PStasklist_dequeue(task);
	if (task->deleted) {
	    answer.error = ECHILD;
	} else {
	    answer.error = spawnTask(task);
	}

	if (answer.error) {
	    /* send only on failure. success reported by forwarder */
	    sendMsg(&answer);
	}
    }
}

/**
 * @brief Drop a PSP_CD_SPAWNREQ[UEST] message.
 *
 * Drop the message @a msg of type PSP_CD_SPAWNREQ[UEST].
 *
 * Since the spawning process waits for a reaction to its request a
 * corresponding answer is created. This work for both generations of
 * spawn-requests.
 *
 * @param msg Pointer to the message to drop.
 *
 * @return No return value.
 */
static void drop_SPAWNREQ(DDBufferMsg_t *msg)
{
    DDErrorMsg_t errmsg;

    errmsg.header.type = PSP_CD_SPAWNFAILED;
    errmsg.header.dest = msg->header.sender;
    errmsg.header.sender = msg->header.dest;
    errmsg.header.len = sizeof(errmsg);

    errmsg.error = EHOSTDOWN;
    errmsg.request = 0;

    sendMsg(&errmsg);
}

void deleteSpawnTasks(PSnodes_ID_t node)
{
    list_t *t;

    PSID_log(PSID_LOG_SPAWN, "%s(%d)\n", __func__, node);

    list_for_each(t, &spawnTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);
	if (PSC_getID(task->tid) == node) task->deleted = 1;
    }
}

/**
 * @brief Cleanup spawning task marked as deleted
 *
 * Actually destroy task-structure waiting to be spawned but marked as
 * deleted. These tasks are expected to be marked via @ref
 * deleteSpawnTasks().
 *
 * @return No return value
 */
static void cleanupSpawnTasks(void)
{
    list_t *t, *tmp;

    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    list_for_each_safe(t, tmp, &spawnTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);

	if (task->deleted) {
	    PStasklist_dequeue(task);
	    PStask_delete(task);
	}
    }
}

/**
 * @brief Handle a PSP_CD_SPAWNSUCCESS message.
 *
 * Handle the message @a msg of type PSP_CD_SPAWNSUCCESS.
 *
 * Register the spawned process to its parent task and forward the
 * message to the initiating process.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_SPAWNSUCCESS(DDErrorMsg_t *msg)
{
    PStask_ID_t tid = msg->header.sender;
    PStask_ID_t ptid = msg->header.dest;
    PStask_t *task;
    char *parent = strdup(PSC_printTID(ptid));

    PSID_log(PSID_LOG_SPAWN, "%s(%s) with parent(%s)\n",
	     __func__, PSC_printTID(tid), parent);

    task = PStasklist_find(&managedTasks, ptid);
    if (task && (task->fd > -1 || task->group == TG_ANY)) {
	/* register the child */
	PSID_setSignal(&task->childList, tid, -1);

	/* child will send a signal on exit, thus include into assignedSigs */
	PSID_setSignal(&task->assignedSigs, tid, -1);
    } else {
	/* task not found, it has already died */
	PSID_log(-1, "%s(%s) with parent(%s) already dead\n",
		 __func__, PSC_printTID(tid), parent);
	PSID_sendSignal(tid, 0, ptid, -1, 0, 0);
    }

    /*
     * Send the initiator the success message.
     *
     * If the initiator is a normal but unconnected task, it has used
     * its forwarder as a proxy via PMI. Thus, send SPAWNSUCCESS to
     * the proxy.
     */
    if (task && task->group == TG_ANY && task->fd == -1) {
	msg->header.dest = task->forwardertid;
    }
    sendMsg(msg);
    free(parent);
}

/**
 * @brief Handle a PSP_CD_SPAWNFAILED message.
 *
 * Handle the message @a msg of type PSP_CD_SPAWNFAILED.
 *
 * This might have been created by a local forwarder. Thus, release
 * this forwarder and forwards the message to the initiating process.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_SPAWNFAILED(DDErrorMsg_t *msg)
{
    PSID_log(PSID_LOG_SPAWN, "%s: %s reports on rank %d", __func__,
	     PSC_printTID(msg->header.sender), msg->request);
    PSID_log(PSID_LOG_SPAWN, " error = %d sending to parent %s\n", msg->error,
	     PSC_printTID(msg->header.dest));

    /* Make sure an old initiator does not get extended messages */
    if (PSIDnodes_getDmnProtoV(PSC_getID(msg->header.dest)) < 404) {
	msg->header.len = sizeof(*msg);
    }

    if (PSC_getID(msg->header.sender) == PSC_getMyID()) {
	/* Forwader will disappear immediately, release it. */
	PStask_t *task = PStasklist_find(&managedTasks, msg->header.sender);

	if (!task) {
	    PSID_log(-1, "%s: task %s not found\n", __func__,
		     PSC_printTID(msg->header.sender));
	} else {
	    task->released = 1;
	    deleteClient(task->fd);
	}
    }

    /* send failure msg to initiator */
    sendMsg(msg);
}

/**
 * @brief Handle a PSP_CD_SPAWNFINISH message.
 *
 * Handle the message @a msg of type PSP_CD_SPAWNFINISH.
 *
 * This just forwards the message to the initiating process.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_SPAWNFINISH(DDMsg_t *msg)
{
    PSID_log(PSID_LOG_SPAWN, "%s: sending to local parent %s\n",
	     __func__, PSC_printTID(msg->dest));

    /* send the initiator a finish msg */
    sendMsg(msg);
}

/**
 * @brief Handle a PSP_DD_CHILDBORN message.
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
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_CHILDBORN(DDErrorMsg_t *msg)
{
    PStask_t *forwarder = PStasklist_find(&managedTasks, msg->header.sender);
    PStask_t *child = PStasklist_find(&managedTasks, msg->request);
    PStask_ID_t succMsgDest = 0;
    int blocked;

    PSID_log(PSID_LOG_SPAWN, "%s: from %s\n", __func__,
	     PSC_printTID(msg->header.sender));
    if (!forwarder) {
	PSID_log(-1, "%s: forwarder %s not found.\n", __func__,
		 PSC_printTID(msg->header.sender));
	return;
    }

    if (child) {
	PSID_log(-1, "%s: child %s", __func__, PSC_printTID(msg->request));
	PSID_log(-1, " already there. forwarder %s missed a message\n",
		 PSC_printTID(msg->header.sender));
	msg->header.type = PSP_DD_CHILDACK;
	msg->header.dest = msg->header.sender;
	msg->header.sender = PSC_getMyTID();
	sendMsg(msg);

	return;
    }

    /* prepare child task */
    blocked = PSID_blockSIGCHLD(1);
    child = PStask_clone(forwarder);
    PSID_blockSIGCHLD(blocked);
    if (!child) {
	PSID_warn(-1, errno, "%s: PStask_clone()", __func__);

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

	return;
    }

    if (forwarder->argv) {
	int i;
	for (i=0; i<forwarder->argc; i++) {
	    if (forwarder->argv[i]) free(forwarder->argv[i]);
	}
	free(forwarder->argv);
	forwarder->argv = NULL;
	forwarder->argc = 0;
    }

    child->tid = msg->request;
    child->fd = -1;
    child->group = forwarder->childGroup;
    child->forwardertid = forwarder->tid;

    /* Accounting info */
    if (child->group != TG_ADMINTASK && child->group != TG_SERVICE
	&& child->group != TG_SERVICE_SIG && child->group != TG_KVS) {
	sendAcctChild(child);
    }

    /* Fix interactive shell's argv[0] */
    if (child->argc == 2 && (!strcmp(child->argv[0], "/bin/bash")
			     && !strcmp(child->argv[1], "-i"))) {
	free(child->argv[0]);
	child->argv[0] = strdup("-bash");
	free(child->argv[1]);
	child->argv[1] = NULL;
	child->argc = 1;
    } else if (child->argc == 2 && (!strcmp(child->argv[0], "/bin/tcsh")
				     && !strcmp(child->argv[1], "-i"))) {
	free(child->argv[0]);
	child->argv[0] = strdup("-tcsh");
	free(child->argv[1]);
	child->argv[1] = NULL;
	child->argc = 1;
    }

    /* Child will get signal from parent. Thus add ptid to assignedSigs */
    PSID_setSignal(&child->assignedSigs, child->ptid, -1);
    /* Enqueue the task right in front of the forwarder */
    PStasklist_enqueue(&forwarder->next, child);
    /* Tell everybody about the new task */
    incJobs(1, (child->group==TG_ANY));

    /* Spawned task will get signal if the forwarder dies unexpectedly. */
    PSID_setSignal(&forwarder->childList, child->tid, -1);

    /*
     * The answer will be sent directly to the initiator if he is on
     * the same node. Thus register directly as his child.
     */
    if (PSC_getID(child->ptid) == PSC_getMyID()) {
	PStask_t *parent = PStasklist_find(&managedTasks, child->ptid);

	if (!parent) {
	    PSID_log(-1, "%s: parent task %s not found\n", __func__,
		     PSC_printTID(child->ptid));
	} else {
	    PSID_setSignal(&parent->childList, child->tid, -1);
	    PSID_setSignal(&parent->assignedSigs, child->tid, -1);

	    /*
	     * If the parent is a normal but unconnected task, it was
	     * not the origin of the spawn but used the forwarder as a
	     * proxy via PMI. Thus, send SPAWNSUCCESS to the proxy.
	     */
	    if (parent->group == TG_ANY && parent->fd == -1) {
		succMsgDest = parent->forwardertid;
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
    msg->error = 0;

    sendMsg(msg);
}

/**
 * @brief Handle a PSP_DD_CHILDDEAD message.
 *
 * Handle the message @a msg of type PSP_DD_CHILDDEAD.
 *
 * This type of message is created by the forwarder process to inform
 * the local daemon on the dead of the controlled client process. This
 * might result in sending pending signals, un-registering the task,
 * etc. Additionally the message will be forwarded to the daemon
 * controlling the parent task in order to take according measures.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_CHILDDEAD(DDErrorMsg_t *msg)
{
    PStask_t *task, *forwarder;

    PSID_log(PSID_LOG_SPAWN, "%s: from %s", __func__,
	     PSC_printTID(msg->header.sender));
    PSID_log(PSID_LOG_SPAWN, " to %s", PSC_printTID(msg->header.dest));
    PSID_log(PSID_LOG_SPAWN, " concerning %s\n", PSC_printTID(msg->request));

    if (msg->header.dest != PSC_getMyTID()) {
	if (PSC_getID(msg->header.dest) != PSC_getMyID()) {
	    /* Destination on foreign node. Forward */
	    sendMsg(msg);
	    return;
	}
	/* Destination on my node, let's take a peek */
	task = PStasklist_find(&managedTasks, msg->header.dest);
	if (!task) return;

	if (PSID_removeSignal(&task->assignedSigs, msg->request, -1)) {
	    /* Neither release nor sig received, send sig now */
	    PSID_log(-1, "%s: Neither signal nor release received for %s",
		     __func__, PSC_printTID(task->tid));
	    PSID_log(-1, " from %s. Sending signal now.\n",
		     PSC_printTID(msg->request));
	    PSID_sendSignal(task->tid, task->uid, msg->request, -1, 0, 0);
	}
	if (!PSID_removeSignal(&task->childList, msg->request, -1)) {
	    /* No child found. Might already be inherited by parent */
	    if (task->ptid) {
		msg->header.dest = task->ptid;

		PSID_log(PSID_LOG_SPAWN,
			 "%s: forward PSP_DD_CHILDDEAD from %s",
			 __func__, PSC_printTID(msg->request));
		PSID_log(PSID_LOG_SPAWN, " dest %s",
			 PSC_printTID(task->tid));
		PSID_log(PSID_LOG_SPAWN, "->%s\n",
			 PSC_printTID(task->ptid));

		sendMsg(msg);
	    }
	    /* To be sure, mark child as released */
	    PSID_log(PSID_LOG_SPAWN, "%s: %s not (yet?) child of",
		     __func__, PSC_printTID(msg->request));
	    PSID_log(PSID_LOG_SPAWN, " %s\n", PSC_printTID(task->tid));
	    PSID_setSignal(&task->deadBefore, msg->request, -1);
	}

	if (task->removeIt && PSID_emptySigList(&task->childList)) {
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
	    sendMsg(msg);
	    break;
	case TG_SERVICE_SIG:
	    /* service task requested signal */
	    if (!WIFEXITED(msg->error) || WIFSIGNALED(msg->error)
		|| PSID_emptySigList(&task->childList)) {
		PSID_sendSignal(task->tid, task->uid, msg->request, -1, 0, 0);
	    }
	    break;
	default:
	    /* Do nothing */
	    break;
	}
	return;
    }

    /* Release the corresponding forwarder */
    forwarder = PStasklist_find(&managedTasks, msg->header.sender);
    if (forwarder) {
	forwarder->released = 1;
	PSID_removeSignal(&forwarder->childList, msg->request, -1);
    } else {
	/* Forwarder not found */
	PSID_log(-1, "%s: forwarder task %s not found\n",
		 __func__, PSC_printTID(msg->header.sender));
    }

    /* Try to find the task */
    task = PStasklist_find(&managedTasks, msg->request);

    if (!task) {
	/* task not found */
	/* This is not critical. Task has been removed by deleteClient() */
	PSID_log(PSID_LOG_SPAWN, "%s: task %s not found\n", __func__,
		 PSC_printTID(msg->request));
    } else {
	int destDmnPSPver = PSIDnodes_getDmnProtoV(PSC_getID(task->loggertid));
	if (destDmnPSPver > 409
	    && !(task->group == TG_SERVICE || task->group == TG_SERVICE_SIG
		 || task->group == TG_ADMINTASK || task->group == TG_KVS) ) {
	    /** Create and send PSP_DD_CHILDRESREL message */
	    DDBufferMsg_t resRelMsg = (DDBufferMsg_t) {
		.header = (DDMsg_t) {
		    .type = PSP_DD_CHILDRESREL,
		    .dest = task->loggertid,
		    .sender = msg->request,
		    .len = sizeof(resRelMsg.header)},
		.buf = {0} };
	    char *ptr = resRelMsg.buf;

	    unsigned short nBytes = PSCPU_bytesForCPUs(
		PSIDnodes_getVirtCPUs(PSC_getMyID()));
	    *(uint16_t *)ptr = nBytes;
	    ptr += sizeof(int16_t);
	    resRelMsg.header.len += sizeof(int16_t);

	    PSCPU_extract(ptr, task->CPUset, nBytes);
	    //ptr += nBytes;
	    resRelMsg.header.len += nBytes;

	    PSID_log(PSID_LOG_SPAWN, "%s: PSP_DD_CHILDRESREL on %s to %d\n",
		     __func__, PSC_printTID(task->tid),
		     PSC_getID(task->loggertid));

	    if (sendMsg(&resRelMsg) < 0) {
		PSID_warn(-1, errno,
			  "%s: send PSP_DD_CHILDRESREL to node %d failed",
			  __func__, PSC_getID(resRelMsg.header.dest));
	    }
	}

	/* Prepare CHILDDEAD msg here. Task might be removed in next step */
	msg->header.dest = task->ptid;
	msg->header.sender = PSC_getMyTID();

	/* child is dead now; thus, remove parent from assignedSigs */
	PSID_removeSignal(&task->assignedSigs, task->ptid, -1);

	/* If child not connected, remove task from tasklist. This
	 * will also send all signals */
	if (task->fd == -1) {
	    PSID_log(PSID_LOG_TASK, "%s: PStask_cleanup()\n", __func__);
	    PStask_cleanup(msg->request);
	}

	/* Send CHILDDEAD to parent */
	msg_CHILDDEAD(msg);
    }
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
    int blocked;

    blocked = PSID_blockSIGCHLD(1);

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
		    PStask_cleanup(task->tid);
		} else {
		    task->deleted = 1;
		}
	    }
	}
    }

    PSID_blockSIGCHLD(blocked);
}


void initSpawn(void)
{
    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    PSID_registerMsg(PSP_CD_SPAWNREQUEST, msg_SPAWNREQUEST);
    PSID_registerMsg(PSP_CD_SPAWNREQ, (handlerFunc_t) msg_SPAWNREQ);
    PSID_registerMsg(PSP_CD_SPAWNSUCCESS, (handlerFunc_t) msg_SPAWNSUCCESS);
    PSID_registerMsg(PSP_CD_SPAWNFAILED, (handlerFunc_t) msg_SPAWNFAILED);
    PSID_registerMsg(PSP_CD_SPAWNFINISH, (handlerFunc_t) msg_SPAWNFINISH);
    PSID_registerMsg(PSP_DD_CHILDDEAD, (handlerFunc_t) msg_CHILDDEAD);
    PSID_registerMsg(PSP_DD_CHILDBORN, (handlerFunc_t) msg_CHILDBORN);
    PSID_registerMsg(PSP_DD_CHILDACK, (handlerFunc_t) sendClient);

    PSID_registerDropper(PSP_CD_SPAWNREQUEST, drop_SPAWNREQ);
    PSID_registerDropper(PSP_CD_SPAWNREQ, drop_SPAWNREQ);

    PSID_registerLoopAct(checkObstinateTasks);
    PSID_registerLoopAct(cleanupSpawnTasks);
}
