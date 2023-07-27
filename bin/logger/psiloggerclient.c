/*
 * ParaStation
 *
 * Copyright (C) 2009-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psiloggerclient.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "list.h"
#include "pscommon.h"
#include "selector.h"
#include "psilogger.h"

/** Internal state-masks of clients */
typedef enum {
    CLIENT_ACTIVE =  0x001, /**< Client is active input destination */
    CLIENT_STOPPED = 0x002, /**< Client sent STOP msg, waiting for CONT */
    CLIENT_GONE =    0x004, /**< Client was there but went away */
    CLIENT_REL =     0x008, /**< KVS client release send */
} client_flags_t;

/** Structure holding all available information about clients */
typedef struct {
    PStask_ID_t tid;         /**< Forwarder's task ID */
    client_flags_t flags;    /**< Client's mask of internal states */
    PStask_group_t group;
    list_t next;             /**< Puts client into activeClients list */
} client_t;

/** Array holding all the information of the currently known clients */
static client_t *clients = NULL;

/** List holding all the connected clients receiving input */
static list_t activeClients;

/** Number of currently connected clients */
static int nClnts = -1;

/** Number of clients currently marked to expect input */
static int nActvClnts;

/**
 * Number of clients currently marked to expect input *and*
 * connected. This is the length of the @ref activeClients list.
 */
static int nRecvClnts;

/** Number of clients currently requesting input-stop */
static int nActvSTOPs;

/** Minimum rank currently being handled by this module */
static int minRank = 0;

/** Maximum rank currently being handled by this module */
static int maxRank = -1;

/**
 * Maximum rank this module is currently able to handle. This is
 * determined by the actual size of @ref clients.
 */
static int maxClient = -1;

static int nTaskClnts = -1;

/** The current value of the string describing the input destinations */
static char *destStr = NULL;


int getNoClients(void)
{
    return nClnts;
}

int getMinRank(void)
{
    return minRank;
}

/** An offset that helps to makes sure that assigned service ranks are unique */
static int offsetServiceRank = 0;

int getNextServiceRank(void)
{
     int ret;

     /* return next free (and unique!) service rank: */
     ret = getMinRank() - offsetServiceRank;

     /* keep returned/assigned service ranks unique: */
     offsetServiceRank += 3; /* service process plus KVS provider */

     return ret;
}

int getMaxRank(void)
{
    return maxRank;
}

int getClientRank(PStask_ID_t tid)
{
    int rank = getMinRank();

    if (!clients) {
	PSIlog_log(-1, "%s: Not initialized", __func__);
	PSIlog_finalizeLogs();
	exit(1);
    }

    while ((rank <= getMaxRank()) && (clients[rank].tid != tid)) rank++;

    return rank;
}

PStask_ID_t getClientTID(int rank)
{
    if (!clients) {
	PSIlog_log(-1, "%s: Not initialized", __func__);
	PSIlog_finalizeLogs();
	exit(1);
    }

    if (rank < getMinRank() || rank > getMaxRank()) return -1;

    return clients[rank].tid;
}

/**
 * @brief Mark client to expect input
 *
 * Mark client at rank @a rank to actively expect input.
 *
 * @param rank Rank of the client to be marked as active
 *
 * @return No return value
 */
static inline void addClnt(int rank)
{
    clients[rank].flags |= CLIENT_ACTIVE;
}

/**
 * @brief Mark client to not expect input
 *
 * Mark client at rank @a rank to not any longer expect input.
 *
 * @param rank Rank of the client to be marked as passive
 *
 * @return No return value
 */
static inline void remClnt(int rank)
{
    clients[rank].flags &= ~CLIENT_ACTIVE;
}

bool clientIsActive(int rank)
{
    if (!clients) {
	PSIlog_log(-1, "%s: Not initialized", __func__);
	PSIlog_finalizeLogs();
	exit(1);
    }

    if (rank < getMinRank() || rank > getMaxRank()) return false;

    return clients[rank].flags & CLIENT_ACTIVE;
}

bool clientIsGone(int rank)
{
    if (!clients) {
	PSIlog_log(-1, "%s: Not initialized", __func__);
	PSIlog_finalizeLogs();
	exit(1);
    }

    if (rank < getMinRank() || rank > getMaxRank()) return false;

    return clients[rank].flags & CLIENT_GONE;
}

/**
 * @brief Test, if client is stopped
 *
 * Test, if the client with rank @a rank is stopped. A client is
 * marked es stopped, if a STOP message was received without a
 * matching CONT message afterwards.
 *
 * @param rank Rank of the client to be tested.
 *
 * @return If the client is marked as stopped, this function returns
 * true. Otherwise false is returned.
 */
static bool clientIsStopped(int rank)
{
    if (!clients) {
	PSIlog_log(-1, "%s: Not initialized", __func__);
	PSIlog_finalizeLogs();
	exit(1);
    }

    if (rank < getMinRank() || rank > getMaxRank()) return false;

    return clients[rank].flags & CLIENT_STOPPED;
}

bool allActiveThere(void)
{
    return (nRecvClnts == nActvClnts);
}

/**
 * @brief Grow internal structures
 *
 * Grow the internal structures use to store all the information
 * necessary to manage to clients and the corresponding
 * input-forwarding. After calling this function, the structures are
 * sufficient to store information for clients starting on rank @a
 * newMin upto clients on rank @a newMax.
 *
 * If it's impossible to get the required resources the function might
 * exit the calling process.
 *
 * @param newMin New minimum rank able to be managed after the call
 * has finished successfully.
 *
 * @param newMax New maximum rank able to be managed after the call
 * has finished successfully.
 *
 * @return No return value
 */
static void growClients(int newMin, int newMax)
{
    client_t *tmp;
    int i;

    if (newMin > getMinRank() || newMax < maxClient) {
	PSIlog_log(-1, "%s: Do not shrink clients.\n", __func__);
	PSIlog_finalizeLogs();
	exit(1);
    }

    if (newMin == getMinRank() && newMax == maxClient) return;

    if (clients && newMin < getMinRank()) {
	tmp = malloc(sizeof(*tmp) * (newMax - newMin + 1));
	if (!tmp) PSIlog_exit(ENOMEM, "%s: malloc()", __func__);

	/* Copy old stuff */
	memcpy(tmp + (minRank-newMin), clients + minRank,
	       sizeof(*tmp)*(maxClient-minRank+1));
	free(clients + minRank);

	for (i = 0; i < minRank-newMin; i++) {
	    tmp[i].tid = -1;
	    tmp[i].flags = 0;
	    INIT_LIST_HEAD(&tmp[i].next);
	}
    } else {
	tmp = clients ? clients + minRank : NULL;

	tmp = realloc(tmp, sizeof(*tmp) * (newMax - newMin + 1));
	if (!tmp) PSIlog_exit(ENOMEM, "%s: realloc()", __func__);
    }

    clients = tmp - newMin;

    for (i = maxClient+1; i <= newMax; i++) {
	clients[i].tid = -1;
	clients[i].flags = 0;
	INIT_LIST_HEAD(&clients[i].next);
    }

    minRank = newMin;
    maxClient = newMax;

    setupDestList(NULL);
}

void initClients(int minClientRank, int maxClientRank)
{
    minRank = minClientRank;
    maxRank = maxClientRank;

    maxClient = minClientRank-1;

    growClients(minClientRank, maxClientRank);

    nClnts = 0;
    nTaskClnts = 0;
}

bool registerClient(int rank, PStask_ID_t tid, PStask_group_t group)
{
    int oldMaxRank = getMaxRank();

    if (!clients) {
	PSIlog_log(-1, "%s: Not initialized", __func__);
	PSIlog_finalizeLogs();
	exit(1);
    }

    if (rank < getMinRank()) {
	 offsetServiceRank -= (minRank - rank);
	 growClients(rank, maxClient);
    }

    if (rank > maxClient) {
	growClients(getMinRank(), 2*rank);
    }
    if (rank > getMaxRank()) {
	maxRank = rank;
	if (destStr) {
	    const char delimiters[] ="[], \n";
	    char *saveptr, *rankStr = strtok_r(destStr, delimiters, &saveptr);

	    if (rankStr && !strncasecmp(rankStr, "all", 3)) {
		int r;
		for (r = oldMaxRank+1; r <= getMaxRank(); r++) {
		    addClnt(r);
		    nActvClnts++;
		}
	    }
	}
    }

    if (clients[rank].tid != -1) {
	if (clients[rank].tid == tid) {
	    PSIlog_log(-1, "%s: %s (rank %d) already connected.\n",
		       __func__, PSC_printTID(tid), rank);
	} else {
	    PSIlog_log(-1, "%s: %s (rank %d)", __func__,
		       PSC_printTID(clients[rank].tid), rank);
	    PSIlog_log(-1, " connects as %s.\n", PSC_printTID(tid));
	}
	return false;
    }

    clients[rank].tid = tid;
    clients[rank].group = group;
    nClnts++;
    if (rank > -1) nTaskClnts++;

    if (clientIsActive(rank)) {
	list_add_tail(&clients[rank].next, &activeClients);
	nRecvClnts++;
    }

    PSIlog_log(PSILOG_LOG_VERB, "%s: new connection from %s (%d)\n", __func__,
	       PSC_printTID(tid), rank);

    return true;
}

void deregisterClient(int rank)
{
    int i;

    if (!clients) {
	PSIlog_log(-1, "%s: Not initialized", __func__);
	PSIlog_finalizeLogs();
	exit(1);
    }

    if (rank < getMinRank() || rank > getMaxRank())
	PSIlog_exit(EINVAL, "%s", __func__);

    if (getClientTID(rank) == -1) {
	int key = clientIsGone(rank) ? PSILOG_LOG_VERB : -1;
	PSIlog_log(key, "%s: rank %d not registered.\n", __func__, rank);
	return;
    }

    clients[rank].tid = -1;
    nClnts--;
    clients[rank].flags |= CLIENT_GONE;

    if (rank > -1) {
	if (!(--nTaskClnts)) {
	    /* last regular child left -> tell all service processes to exit */
	    for (i = getMinRank(); i < 0; i++) {
		if (clients[i].tid != -1 && clients[i].group == TG_KVS
		    && !(clients[i].flags & CLIENT_REL)) {
		    PSLog_write(clients[i].tid, SERV_EXT, NULL, 0);
		    clients[i].flags |= CLIENT_REL;
		}
	    }
	}
    }

    if (clientIsActive(rank)) {
	if (clientIsStopped(rank)) nActvSTOPs--;
	if (! list_empty(&clients[rank].next)) {
	    list_del_init(&clients[rank].next);
	    nRecvClnts--;
	}
    }
}

void handleSTOPMsg(PSLog_Msg_t *msg)
{
    int rank = msg->sender;
    PStask_ID_t tid = getClientTID(rank);

    /* rank InputDest wants pause */
    if (tid == -1 || tid != msg->header.sender) {
	PSIlog_log(-1, "STOP from wrong rank: %d\n", rank);
	return;
    }

    if (clientIsActive(rank) && !clientIsStopped(rank)) {
	if (!nActvSTOPs) {
	    if (Selector_isRegistered(STDIN_FILENO))
		Selector_disable(STDIN_FILENO);
	    PSIlog_log(PSILOG_LOG_VERB, "forward input is paused\n");
	}
	nActvSTOPs++;
    }
    clients[rank].flags |= CLIENT_STOPPED;
}

void handleCONTMsg(PSLog_Msg_t *msg)
{
    int rank = msg->sender;
    PStask_ID_t tid = getClientTID(rank);

    if (tid == -1 || tid != msg->header.sender) {
	PSIlog_log(-1, "CONT from wrong rank: %d\n", rank);
	return;
    }

    if (clientIsActive(rank) && clientIsStopped(rank)) {
	nActvSTOPs--;
	if (!nActvSTOPs && allActiveThere()) {
	    if (Selector_isRegistered(STDIN_FILENO))
		Selector_enable(STDIN_FILENO);
	    PSIlog_log(PSILOG_LOG_VERB, "forward input continues\n");
	}
    }
    clients[rank].flags &= ~CLIENT_STOPPED;
}

static bool daemonCommStopped = false;

void handleSENDSTOP(DDBufferMsg_t *msg)
{
    /* some daemon wants pause */
    if (!daemonCommStopped) {
	if (!nActvSTOPs) {
	    if (Selector_isRegistered(STDIN_FILENO))
		Selector_disable(STDIN_FILENO);
	    PSIlog_log(PSILOG_LOG_VERB, "forward input is paused\n");
	}
	nActvSTOPs++;
    }
    daemonCommStopped = true;
}

void handleSENDCONT(DDBufferMsg_t *msg)
{
    /* some daemon wants continue */
    if (daemonCommStopped) {
	nActvSTOPs--;
	if (!nActvSTOPs && allActiveThere()) {
	    if (Selector_isRegistered(STDIN_FILENO))
		Selector_enable(STDIN_FILENO);
	    PSIlog_log(PSILOG_LOG_VERB, "forward input continues\n");
	}
    }
    daemonCommStopped = false;
}

/**
 * @brief Error in destination list analysis
 *
 * Simple helper function called, if an error in the destination list
 * analysis was detected. A error message is released and the
 * destiation list is reset to rank 0 only.
 *
 * In the rare case where getMinRank() returnes a value larger than 0,
 * this rank is used as the only destination.
 *
 * @return No return value
 */
static inline void destListError(void)
{
    int r;

    PSIlog_log(-1, " for input redirection. Valid ranks: [0-%i]. Setting"
	       " input destination to rank 0.\n", getMaxRank());

    /* reset input destinations */
    for (r = getMinRank(); r <= getMaxRank(); r++) remClnt(r);
    addClnt(getMinRank() > 0 ? getMinRank() : 0);
}

void setupDestList(char *input)
{
    const char delimiters[] ="[], \n";
    char *parseStr, *rankStr, *saveptr;
    int first, last, r, oldSTOPs;

    if (input && (!destStr || strcmp(destStr, input))) {
	free(destStr);
	destStr = strdup(input);
    }

    if (!clients) return;

    if (!destStr) {
	PSIlog_log(-1, "%s: no destination definition\n", __func__);
	PSIlog_finalizeLogs();
	exit(1);
    }

    parseStr = strdup(destStr);

    if (!(rankStr = strtok_r(parseStr, delimiters, &saveptr))) {
	for (r = getMinRank(); r <= getMaxRank(); r++) remClnt(r);
    } else if (!strncasecmp(rankStr, "all", 3)) {
	/* set input to all ranks */
	int start;
	if (getMinRank() < 0) {
	    /* expect rank < 0 to be service processes */
	    for (r = getMinRank(); r < 0; r++) remClnt(r);
	    start = 0;
	} else {
	    start = getMinRank();
	}
	for (r = start; r <= getMaxRank(); r++) addClnt(r);
    } else {
	/* reset old input destinations */
	for (r = getMinRank(); r <= getMaxRank(); r++) remClnt(r);

	while (rankStr) {
	    if (strchr(rankStr, '-')) {
		if ((sscanf(rankStr, "%d-%d", &first, &last)) != 2) {
		    PSIlog_log(-1, "%s: invalid range '%s'\n", __func__,
			       rankStr);
		    destListError();
		    break;
		}
		if (first<getMinRank() || last>getMaxRank() || last<first) {
		    PSIlog_log(-1, "%s: invalid range [%i-%i]\n", __func__,
			       first, last);
		    destListError();
		    break;
		}
		for (r = first; r <= last; r++) addClnt(r);
	    } else {
		char *end;
		r = strtol(rankStr, &end, 10);
		if (rankStr == end || *end) {
		    PSIlog_log(-1, "%s: invalid destination '%s'\n", __func__,
			       rankStr);
		    destListError();
		    break;
		}
		if (r < getMinRank() || r > getMaxRank()) {
		    PSIlog_log(-1, "%s: invalid destination '%i'\n", __func__,
			       r);
		} else {
		    addClnt(r);
		}
	    }
	    rankStr = strtok_r(NULL, delimiters, &saveptr);
	}
    }

    free(parseStr);

    oldSTOPs = nActvSTOPs;

    INIT_LIST_HEAD(&activeClients);
    nActvClnts = 0;
    nRecvClnts = 0;
    nActvSTOPs = 0;

    for (r = getMinRank(); r <= getMaxRank(); r++) {
	INIT_LIST_HEAD(&clients[r].next);
	if (clientIsGone(r)) remClnt(r);
	if (clientIsActive(r)) {
	    nActvClnts++;
	    if (getClientTID(r) != -1) {
		list_add_tail(&clients[r].next, &activeClients);
		nRecvClnts++;
	    }
	    if (clientIsStopped(r)) nActvSTOPs++;
	}
    }

    if (nActvSTOPs) {
	if (Selector_isRegistered(STDIN_FILENO))
	    Selector_disable(STDIN_FILENO);
	if (!oldSTOPs) PSIlog_log(PSILOG_LOG_VERB, "input-forward paused\n");
    } else if (oldSTOPs && allActiveThere()) {
	if (Selector_isRegistered(STDIN_FILENO))
	    Selector_enable(STDIN_FILENO);
	PSIlog_log(PSILOG_LOG_VERB, "input-forward continues\n");
    }
}

static char destDescr[1024];

char *getDestStr(size_t maxLen)
{
    int r = 0, first, last;

    if (maxLen > sizeof(destDescr)) maxLen = sizeof(destDescr);
    destDescr[0] = '\0';

    /* find start of first range */
    while (r <= getMaxRank() && !clientIsActive(r)) r++;

    while (r <= getMaxRank()) {
	first=r;

	/* find last in range */
	while (r <= getMaxRank() && clientIsActive(r)) r++;
	last=r-1;

	/* find start of next range */
	while (r <= getMaxRank() && !clientIsActive(r)) r++;

	snprintf(destDescr + strlen(destDescr), maxLen - strlen(destDescr),
		 "%d", first);
	if (last != first) {
	    snprintf(destDescr + strlen(destDescr), maxLen - strlen(destDescr),
		     "-%d", last);
	}
	snprintf(destDescr + strlen(destDescr), maxLen - strlen(destDescr),
		 "%s", (r > getMaxRank()) ? "" : ",");
    } while (r<=getMaxRank());

    return destDescr;
}

int forwardInputStr(char *buf, size_t len)
{
    list_t *c;

    list_for_each (c, &activeClients) {
	client_t *client = list_entry(c , client_t, next);
	if (client->tid == -1) {
	    PSIlog_log(-1, "%s: No TID for client\n", __func__);
	    errno = EHOSTUNREACH;
	    return -1;
	}

	if (sendMsg(client->tid, STDIN, buf, len) < 0) {
	    PSIlog_log(-1, "%s: Failed to forward STDIN to %s\n", __func__,
		       PSC_printTID(client->tid));
	    return -1;
	}
    }
    return len;
}
