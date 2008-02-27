/*
 *               ParaStation
 *
 * Copyright (C) 2007-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file mpiexec_elan.c Adds support for the Quadrics ELAN library libelan
 * in order to start applications build against their MPI in a ParaStation
 * cluster.
 *
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <dlfcn.h>

#include <elan/elanctrl.h>

#include <pse.h>
#include <psi.h>
#include <psienv.h>
#include <psiinfo.h>
#include <psispawn.h>
#include <pscommon.h>

typedef struct netIDmap_s {
    char *host;
    char *id;
    struct netIDmap_s *next;
} netIDmap_t;

netIDmap_t *NetIDmap = NULL;

/* pointer to the shared elan library */
void *libh = NULL;

/* set to true if loading of elan library was successful */
int isLoaded = 0;

/* function pointer to libelan */
void (* dym_elan_nullcap) (ELAN_CAPABILITY *);
void (* dym_elan_getenvCap) (ELAN_CAPABILITY*, int);


static char *getEntry(char *host)
{
    netIDmap_t *ent = NetIDmap;

    while (ent && strncmp(ent->host, host, strlen(ent->host))) ent = ent->next;

    if (ent) return ent->id;

    return NULL;
}

static void addEntry(char *host, char *id)
{
    netIDmap_t *ent;

    if (!host) {
	printf("%s: host is <nil>\n", __func__);
	return;
    }

    if (!id) {
	printf("%s: id is <nil>\n", __func__);
	return;
    }

    if (getEntry(host)) {
	printf("%s: host '%s' already there\n", __func__, host);
	return;
    }

    ent = malloc(sizeof(*ent));
    if (!ent) {
	printf("%s: no mem\n", __func__);
	return;
    }
    ent->host = strdup(host);
    ent->id = strdup(id);
    ent->next = NetIDmap;
    NetIDmap = ent;
}

#define IDMAPFILE "/etc/elanidmap"

/**
 * @brief Create map of ELAN IDs.
 *
 * Create a map of hostnames and ELAN IDs from the file "/etc/elanidmap".
 *
 * @return No return value.
 */
static void getNetIDmap(void)
{
    FILE *elanIDfile;
    char line[256];

    elanIDfile = fopen(IDMAPFILE, "r");

    if (!elanIDfile) {
	fprintf(stderr, "%s: Could not open '%s':", __func__, IDMAPFILE);
	perror("");
	exit(1);
    }

    while (fgets(line, sizeof(line), elanIDfile)) {
	char *host = strtok(line, " \t\n");
	char *id = strtok(NULL, " \t\n");

	if (!host || *host == '#') continue;

	addEntry(host, id);
    }
    fclose(elanIDfile);
}

/**
 * @brief Free map of ELAN IDs.
 *
 * Free a map of hostnames and ELAN IDs created with @ref getNetIDmap().
 *
 * @return No return value.
 */
static void freeNetIDmap(void)
{
    netIDmap_t *ent;

    while ((ent = NetIDmap)) {
	NetIDmap = ent->next;
	if (ent->host) free(ent->host);
	if (ent->id) free(ent->id);
	free(ent);
    }
}

/*----------------------------------------------------------------------*/
/* Stolen from libelan */

/* environment elan capability name */
/* LIBELAN_ECAP=...    my segment   (index < 0)  */
/* LIBELAN_ECAP0=...   segment 0    (index == 0) */
/* LIBELAN_ECAP1=... segment 1  (index == 1) */
/* etc... */
static char *envName (int index)
{
    static char name[32];

    if (index < 0)
	strcpy (name, "LIBELAN_ECAP");
    else
	sprintf (name, "LIBELAN_ECAP%d", index);

    return (name);
}

static char *capToString(ELAN_CAPABILITY *cap, char *str, size_t len)
{
    char *cp, *tp = str;

    for (cp = (char *) (cap + 1); --cp >= (char *)cap; tp += 2)
	sprintf((char *)tp, "%02x", (*cp) & 0xff);

    return(str);
}

/*----------------------------------------------------------------------*/

static int prepCapEnv(int np, int verbose)
{
    ELAN_CAPABILITY cap;
    int procsPerNode[ELAN_MAX_VPS];
    int n, p, nContexts=1;
    char *nodesFirst = getenv("PSI_LOOP_NODES_FIRST"), envStr[8192];

    dym_elan_nullcap (&cap);
    cap.cap_lowcontext = 64;
    cap.cap_mycontext = 64;
    cap.cap_highcontext = 64;
    cap.cap_lownode = ELAN_MAX_VPS;
    cap.cap_highnode = 0;
    cap.cap_railmask = 1;
    cap.cap_type = nodesFirst ? ELAN_CAP_TYPE_CYCLIC : ELAN_CAP_TYPE_BLOCK;
    cap.cap_type |= ELAN_CAP_TYPE_BROADCASTABLE;

    /* Setup bitmap */
    getNetIDmap();

    for (n=0; n<np; n++) {
	PSnodes_ID_t node;
	struct hostent *hp;
	u_int32_t hostaddr;
	char *ptr, *idStr, *end;
	int id;

	int ret = PSI_infoNodeID(-1, PSP_INFO_RANKID, &n, &node, 1);
	if (ret || (node < 0)) return -1;

	ret = PSI_infoUInt(-1, PSP_INFO_NODE, &node, &hostaddr, 0);
	if (ret || (hostaddr == INADDR_ANY)) return -1;

	hp = gethostbyaddr(&hostaddr, sizeof(hostaddr), AF_INET);

	if (!hp) return -1;

	if ((ptr = strchr (hp->h_name, '.'))) *ptr = '\0';

	idStr = getEntry(hp->h_name);
	if (!idStr) {
	    if (verbose) { 
		printf("%s: No ID found for '%s'\n", __func__, hp->h_name);
	    }
	    return -1;
	}
	id = strtol(idStr, &end, 10);
	if (end == idStr || *end) {
	    if (verbose) { 
		printf("%s: No ID found in '%s'\n", __func__, idStr);
	    }
	    return -1;
	}
	if (id < cap.cap_lownode)
	    cap.cap_lownode = id;

	if (id > cap.cap_highnode)
	    cap.cap_highnode = id;

	procsPerNode[id]++;
	if (procsPerNode[id] > nContexts)
	    nContexts = procsPerNode[id];
    }

    freeNetIDmap();

    for (n = 0; n < cap.cap_highnode - cap.cap_lownode + 1; n++)
	for (p = 0; p < procsPerNode[cap.cap_lownode + n]; p++)
	    BT_SET(cap.cap_bitmap, n*nContexts + p);

    cap.cap_highcontext = cap.cap_lowcontext + nContexts - 1;

    capToString(&cap, envStr, sizeof(envStr));
    //fprintf(stderr, "%s: setting elan env: %s to %s \n", __func__, envName(0), envStr);
    setPSIEnv(envName(0), envStr, 1);

    return 0;
}

/**
 * @brief Load the libelan and init the
 * function pointer into it.
 *
 * @return Returns 1 on success and 0 on error.
 */
int initELAN(void)
{
    libh = dlopen("libelanctrl.so", RTLD_NOW | RTLD_GLOBAL);
    if (!libh) {
	return 0;
    }

    dym_elan_nullcap = dlsym(libh, "elan_nullcap");
    dym_elan_getenvCap = dlsym(libh, "elan_getenvCap");

    if (!dym_elan_nullcap || !dym_elan_getenvCap) {
	return 0;
    }

    isLoaded = 1;

    return 1;
}

/**
 * @brief Unload libelan.
 *
 * @return Returns 1 on success and 0 on error.
 */
void closeELAN(void)
{
    if (!isLoaded) {
	return;
    }

    if (libh) dlclose(libh);
    dym_elan_nullcap = NULL;
    dym_elan_getenvCap = NULL;
    isLoaded = 0;
}

/**
 * @brief Setup the environment for the given rank.
 *
 * @param rank The rank of the process to setup.
 *
 * @return Returns 1 on success and 0 on error.
 */
int setupELANProcsEnv(int rank)
{
    PSnodes_ID_t node;
    u_int32_t hostaddr;
    static ELAN_CAPABILITY *cap = NULL;
    static int *numProcs = NULL;
    char envStr[8192];

    /* Loading shared library failed */
    if (!isLoaded) {
	return 0;
    }

    /* Prepare the environment */
    setPSIEnv("LIBELAN_SHMKEY", PSC_printTID(PSC_getMyTID()), 1);

/*  if (getenv("LIBELAN_MACHINES_FILE")) { */
/*	setPSIEnv("LIBELAN_MACHINES_FILE", getenv("LIBELAN_MACHINES_FILE"), 1); */
/*  } */

    int ret = PSI_infoNodeID(-1, PSP_INFO_RANKID, &rank, &node, 1);
    if (ret || (node < 0)) exit(10);

    ret = PSI_infoUInt(-1, PSP_INFO_NODE, &node, &hostaddr, 0);
    if (ret || (hostaddr == INADDR_ANY)) exit(10);

    if (!cap) {
	cap = malloc(sizeof(*cap));
	(*dym_elan_getenvCap)(cap, 0);
    }
    if (!numProcs) numProcs = calloc(sizeof(int), PSC_getNrOfNodes());

    cap->cap_mycontext = cap->cap_lowcontext + numProcs[node];
    numProcs[node]++;

    capToString(cap, envStr, sizeof(envStr));
    //fprintf(stderr, "%s: setting elan env: %s to %s \n", __func__, envName(0), envStr);
    setPSIEnv(envName(0), envStr, 1);

    return 1;
}

int setupELANEnv(int np, int verbose)
{
    /* Check if loading libelan was successful */
    if (!isLoaded) {
	return 0;
    }

    if (prepCapEnv(np, verbose)<0) {
	return 0; 
    }

    return 1;
}
