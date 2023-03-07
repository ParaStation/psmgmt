/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#define _GNU_SOURCE
#include "psmomcollect.h"

#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/statfs.h>
#include <sys/utsname.h>

#include "list.h"
#include "pluginconfig.h"
#include "pluginmalloc.h"

#include "psaccounthandles.h"

#include "psmom.h"
#include "psmomconfig.h"
#include "psmomjob.h"
#include "psmomlog.h"

#define PROC_MEMINFO_PATH   "/proc/meminfo"
#define PROC_CPUINFO_PATH   "/proc/cpuinfo"
#define PROC_LOADINFO_PATH  "/proc/loadavg"
#define PROC_NETLOAD_PATH   "/proc/net/dev"

static char buffer[1000];
/*
Not implemented:
**      idletime        seconds of idle time -> watch dev tty's for date entrys

Obsolete?
**      pids            list of pids in a session
**      quota           quota information (sizes in kb)

Implemented:
 * psmom
**      walltime        wall clock time for a job
**      loadave         current load average # for busy check # format: "%.2f"
**      ncpus           number of cpus
**      totmem          total memory size in KB
**      availmem        available memory size in KB
**      physmem         physical memory size in KB
**      netload         # bytes transferred on all interfaces (/proc/net/dev)
**      size            size of a file or filesystem

 * psaccount plugin
**      cput            cpu time for a pid or session
**      mem             memory size for a pid or session in KB
**      resi            resident memory size for a pid or session in KB
**      sessions        list of sessions in the system
**      nsessions       number of sessions in the system
**      nusers          number of users in the system
*/

/**
 * @brief Set netload information.
 *
 * @return No return value.
 */
static void setNetload(void)
{
    FILE *fp;
    unsigned long long allBytes;
    unsigned long bytesRecv, bytesSend;
    static int readError = 0;
    int res;
    char *line = NULL;
    char interface[50];
    size_t len = 0;
    static char net_format[] = "%*[ ]%30[^:]: %lu %*lu %*d %*d %*d"
	" %*d %*d %*d %lu %*lu %*d %*d %*d %*d %*d %*d";

    if (readError) return;

    if (!(fp = fopen(PROC_NETLOAD_PATH, "r"))) {
	mlog("%s: reading netload failed\n", __func__);
	setEntry(&infoData.list, "netload", "", "netload=");
	readError = 1;
	return;
    }

    allBytes = 0;

    while ((getline(&line, &len, fp)) != -1) {
	if (!(strchr(line, ':'))) continue;

	res = sscanf(line, net_format,
		    interface,
		    &bytesRecv,
		    &bytesSend);

	if (res < 3) {
	    continue;
	}

	/* skip loopback devices */
	if (!(strcmp(interface, "lo"))) continue;

	allBytes += bytesRecv + bytesSend;
    }
    fclose(fp);
    ufree(line);

    snprintf(buffer, sizeof(buffer), "netload=%llu", allBytes);
    setEntry(&infoData.list, "netload", "", buffer);
}

/**
 * @brief Set session information.
 *
 * This information will be collected by the psaccount plugin.
 *
 * @return No return value.
 */
static void setSessions(void)
{
    char buf[500];
    int count, userCount;

    psAccountGetSessionInfos(&count, buf, sizeof(buf), &userCount);

    snprintf(buffer, sizeof(buffer), "sessions=%s", buf);
    setEntry(&infoData.list, "sessions", "", buffer);

    snprintf(buffer, sizeof(buffer), "nsessions=%i", count);
    setEntry(&infoData.list, "nsessions", "", buffer);

    snprintf(buffer, sizeof(buffer), "nusers=%i", userCount);
    setEntry(&infoData.list, "nusers", "", buffer);
}

/**
 * @brief Set job information of running jobs.
 *
 * @return No return value.
 */
static void setJobs(void)
{
    char *jobstring = NULL;

    if (!(jobstring = getJobString())) {
	setEntry(&infoData.list, "jobs", "", "jobs=");
    } else {
	snprintf(buffer, sizeof(buffer), "jobs=%s", jobstring);
	setEntry(&infoData.list, "jobs", "", buffer);
	ufree(jobstring);
    }
}

/**
 * Set the memory information.
 *
 * Read various memory information from the /proc filesystem. This
 * information is node specific, not job specific.
 *
 * @return No return value.
 */
static void setMemory(void)
{
    FILE *fp;
    char *line = NULL;
    char unity[31], next[31];
    long long memTotal, memFree, swapFree, swapTotal;
    size_t len = 0;
    int res = 0;
    unsigned long long mem;
    static int readError = 0;

    if (readError) return;
    memTotal = memFree = swapFree = swapTotal = -1;

    if (!(fp = fopen(PROC_MEMINFO_PATH, "r"))) {
	mwarn(errno, "%s: reading meminfo failed\n", __func__);
	setEntry(&infoData.list, "physmem", "", "physmem=");
	setEntry(&infoData.list, "totmem", "", "totmem=");
	setEntry(&infoData.list, "availmem", "", "availmem=");
	readError = 1;
	return;
    }

    while ((getline(&line, &len, fp)) != -1) {
	res = sscanf(line, "%30s %llu %30s",
		    next,
		    &mem,
		    unity);

	if (res < 2) {
	    mlog("%s: reading memory information failed:%s\n", __func__, line);
	    setEntry(&infoData.list, "physmem", "", "physmem=");
	    setEntry(&infoData.list, "totmem", "", "totmem=");
	    setEntry(&infoData.list, "availmem", "", "availmem=");
	    fclose(fp);
	    readError = 1;
	    return;
	}

	if (!(strcmp(next, "MemTotal:"))) {
	    memTotal = mem;
	} else if (!(strcmp(next, "MemFree:"))) {
	    memFree = mem;
	} else if (!(strcmp(next, "SwapFree:"))) {
	    swapFree = mem;
	} else if (!(strcmp(next, "SwapTotal:"))) {
	    swapTotal = mem;
	}
    }

    ufree(line);
    fclose(fp);

    /* available mem = mem_free + swap_free */
    if (memFree >= 0 && swapFree >= 0) {
	snprintf(buffer, sizeof(buffer), "availmem=%lldkb", memFree + swapFree);
	setEntry(&infoData.list, "availmem", "", buffer);
    } else {
	setEntry(&infoData.list, "availmem", "", "availmem=");
    }

    /* total mem  = mem_total + swap_total*/
    if (memFree >= 0 && swapFree >= 0) {
	snprintf(buffer, sizeof(buffer), "totmem=%lldkb", memTotal + swapTotal);
	setEntry(&infoData.list, "totmem", "", buffer);
    } else {
	setEntry(&infoData.list, "totmem", "", "totmem=");
    }

    /* physical mem */
    if (memTotal >= 0) {
	snprintf(buffer, sizeof(buffer), "physmem=%lldkb", memTotal);
	setEntry(&infoData.list, "physmem", "", buffer);
    } else {
	setEntry(&infoData.list, "physmem", "", "physmem=");
    }
}

/**
 * Set the load average.
 *
 * Read load informations from /proc filesystem.
 *
 * @return No return value.
 */
static void setLoad(void)
{
    FILE *fp;
    float avgLoad;
    static int readError = 0;

    if (readError) return;

    if (!(fp = fopen(PROC_LOADINFO_PATH, "r"))) {
	mwarn(errno, "%s: reading loadinfo failed\n", __func__);
	setEntry(&infoData.list, "loadave", "", "loadave=");
	readError = 1;
	return;
    }

    if ((fscanf(fp, "%f", &avgLoad)) != 1) {
	mwarn(errno, "%s: reading loadinfo failed\n", __func__);
	setEntry(&infoData.list, "loadave", "", "loadave=");
	fclose(fp);
	readError = 1;
	return;
    }

    fclose(fp);
    snprintf(buffer, sizeof(buffer), "loadave=%.2f", avgLoad);
    setEntry(&infoData.list, "loadave", "", buffer);
}

/**
 * @brief Set psmom state info.
 *
 * @return No return value.
 */
void setPsmomState(char *state)
{
    static char stateBuf[50] = "state=free";

    if (state) {
	snprintf(stateBuf, sizeof(stateBuf), "state=%s", state);
    }

    setEntry(&staticInfoData.list, "state", "", stateBuf);
}

/**
 * Set information from uname.
 *
 * Set the uname information in the sInfo structure.
 * This information will be forwarded to the pbs_server, and
 * is displayed with the command 'pbsnodes'.
 *
 * @return No return value.
 */
static void setUname(void)
{
    size_t len;
    struct utsname name;
    char *unamestr;

    if (uname(&name) == -1) {
	mwarn(errno, "%s: uname failed\n", __func__);
	setEntry(&staticInfoData.list, "uname", "", "uname=");
	return;
    }

    len = strlen(name.sysname) + strlen(name.nodename) + strlen(name.release) +
	  strlen(name.version) + strlen(name.machine) + 6 + 4 + 1;

    unamestr = umalloc(len);
    snprintf(buffer, sizeof(buffer), "uname=%s %s %s %s %s", name.sysname,
		   name.nodename, name.release, name.version, name.machine);

    setEntry(&staticInfoData.list, "uname", "", buffer);
    ufree(unamestr);
}

/**
 * @brief Set the number of cpus.
 *
 * @return No return value.
 */
static void setNCpus(void)
{
    size_t count = 0;
    char next[100];
    FILE *fp;

    if (!(fp = fopen(PROC_CPUINFO_PATH, "r"))) {
	mwarn(errno, "%s: reading cpuinfo failed\n", __func__);
	setEntry(&staticInfoData.list, "ncpus", "", "ncpus=");
	return;
    }

    while ((fscanf(fp, "%50s %*[^\n]%*c", next)) != EOF) {
	if (!(strcmp(next, "processor"))) {
	    count++;
	}
    }

    if (count  > 0) {
	snprintf(buffer, sizeof(buffer), "ncpus=%zu", count);
	setEntry(&staticInfoData.list, "ncpus", "", buffer);
    } else {
	setEntry(&staticInfoData.list, "ncpus", "", "ncpus=");
    }

    fclose(fp);
}

/**
 * @brief Set filesystem size info.
 *
 * @return No return value.
 */
static void setFsSize(void)
{
    struct statfs st;
    char *param = getConfValueC(config, "REPORT_FS_SIZE");
    static bool report = true;

    if (!report) return;

    if (param) {
	if (statfs(param, &st) == -1) {
	    mlog("%s: statfs(%s) failed : %s, invalid REPORT_FS_SIZE?\n",
		__func__, param, strerror(errno));
	    setEntry(&infoData.list, "size", "", "size=");
	    report = false;
	    return;
	}

	snprintf(buffer, sizeof(buffer), "size=%" PRIu64 "kb:%" PRIu64 "kb",
	    (((uint64_t)st.f_bsize * (uint64_t)st.f_bfree) / 1024),
	    (((uint64_t)st.f_bsize * (uint64_t)st.f_blocks) / 1024));
	setEntry(&infoData.list, "size", "", buffer);
    }
}

/**
 * @brief Set the operation system information.
 *
 * This information is used by the scheduler only.
 *
 * @return No return value.
 */
static void setOpsys(void)
{
    char *param = getConfValueC(config, "SET_OPSYS");
    if (param) {
	snprintf(buffer, sizeof(buffer), "opsys=%s", param);
	setEntry(&staticInfoData.list, "opsys", "", buffer);
    } else {
	/* set to default */
	setEntry(&staticInfoData.list, "opsys", "", "opsys=linux");
    }
}

/**
 * @brief Set the architecture information.
 *
 * This information is used by the scheduler only.
 *
 * @return No return value.
 */
static void setArch(void)
{
    char *param = getConfValueC(config, "SET_ARCH");
    if (param) {
	snprintf(buffer, sizeof(buffer), "arch=%s", param);
	setEntry(&staticInfoData.list, "arch", "", buffer);
    }
}

/**
 * @brief Set psmom version information.
 *
 * @return No return value.
 */
static void setVersion(void)
{
    char ver[50];

    setEntry(&staticInfoData.list, "version", "", "version=psmom-5.2.10");
    snprintf(ver, sizeof(ver), "plugin_ver=%i", version);
    setEntry(&staticInfoData.list, "plugin_ver", "", ver);
}

/**
 * @brief Set variable attribute.
 *
 * @return No return value.
 */
static void setVarAttr(void)
{
    setEntry(&staticInfoData.list, "varattr", "", "varattr=");
}

void updateInfoList(int all)
{
    /* update dynamic information */
    setMemory();
    setLoad();
    setJobs();
    setSessions();
    setNetload();
    setFsSize();
}

Data_Entry_t infoData;
Data_Entry_t staticInfoData;

void initInfoList()
{
    static int init = 0;

    if (!init) {
	init = 1;

	/* init the infoData list */
	INIT_LIST_HEAD(&infoData.list);
	INIT_LIST_HEAD(&staticInfoData.list);

	/* update static information */
	setUname();
	setNCpus();
	setVersion();
	setVarAttr();
	setArch();
	setOpsys();
	setPsmomState(NULL);
    }

    /* update variable information */
    updateInfoList(1);
}
