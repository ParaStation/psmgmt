/*
 *               ParaStation
 *
 * Copyright (C) 2007-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 * @author
 * Michael Rauh <rauh@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "list.h"
#include "pslog.h"
#include "psilogger.h"
#include "psiloggerclient.h"

#include "psiloggermerge.h"

/**
 * Structure for each client which holds the pointers to the
 * cached messages.
 */
typedef struct {
    time_t time;
    char *line;
    struct list_head list;
    int outfd;
    int *counter;
} OutputBuffers;

/**
 * List of received output msgs (the actual buffer).
 */
typedef struct {
    int hash;
    char *line;
    int counter;
    struct list_head list;
} bMessages;

/**
 * Maximum number of processes in this job.
 */
extern int np;

/**
 * Len of the prefix if all ranks do the same output.
 */
static int prelen;

/**
 * Structure for each client which holds the pointers to the
 * cached messages (points into messageCache).
 */
OutputBuffers *ClientOutBuf;

/**
 * All received messages are cached in this structure.
 */
bMessages messageCache;

/**
 * Cache for incomplete lines.
 */
char **BufInc;

/**
 * Set the maxima depth (rows) for searching for equal
 * output lines in the cache.
 *
 * in the matrix. -1 for infinit depth.
 */
int maxMergeDepth = 200;

/**
 * Set the time in seconds who long a received output msg
 * will maximal be hold back.
 */
int maxMergeWait = 2;

/**
 * Enable debbuging messages.
 */
int db = 0;

/**
 * @brief Malloc with error handling.
 *
 * Call malloc() and handle errors.
 *
 * @param size Size in bytes to allocate.
 *
 * @param func Name of function that called. Used for error message.
 *
 * @return Returned is a pointer to the allocated memory.
 */
static void *umalloc(size_t size, const char *func)
{
    void *ptr = malloc(size);

    if (!ptr) {
	PSIlog_log(-1, "%s: malloc() failed.\n", func);
	exit(EXIT_FAILURE);
    }
    return ptr;
}

/**
 * @brief Realloc with error handling.
 *
 * Call realloc() and handle errors.
 *
 * @param size Size in bytes to allocate.
 *
 * @param func Name of function that called. Used for error message.
 *
 * @return Returned is a pointer to the allocated memory.
 */
static void *urealloc(void *old ,size_t size, const char *func)
{
    void *ptr = realloc(old, size);

    if (!ptr) {
	PSIlog_log(-1, "PSIlogger: %s: realloc) failed.\n", func);
	exit(EXIT_FAILURE);
    }
    return ptr;
}

/**
 * @brief Dump global buffer.
 *
 * Just for debbuging purpose, print the global message buffer.
 *
 * @return No return value.
 */
static void dumpGlobalBuffer()
{
    struct list_head *npos;
    bMessages *nval;

    PSIlog_stderr(-1, "\n");
    PSIlog_log(-1, "Dumping output buffer:\n");
    if (!list_empty(&messageCache.list)) {
	list_for_each(npos, &messageCache.list) {
	    nval = list_entry(npos, bMessages, list);
	    if (!nval) {
		break;
	    }
	    if (nval->line) {
		PSIlog_log(-1, "counter:%i hash:%i line:%s", nval->counter,
			   nval->hash, nval->line);
	    }
	}
    }
    PSIlog_log(-1, "dump end\n");
}

/**
 * @brief Dump client buffer.
 *
 * Just for debbuging purpose, print the client message buffer.
 *
 * @return No return value.
 */
static void dumpClientBuffer()
{
    struct list_head *npos;
    OutputBuffers *nval;
    int x;

    PSIlog_stderr(-1, "\n");
    PSIlog_log(-1, "Dumping output buffer:\n");
    for(x=0; x< np; x++) {

	if (!list_empty(&ClientOutBuf[x].list)) {
	    list_for_each(npos, &ClientOutBuf[x].list) {
		/* get data to compare */
		nval = list_entry(npos, OutputBuffers, list);

		if (!nval) break;
		if (nval->line) {
		    PSIlog_log(-1, "client:%i outfd:%d line:%s", x,
			       nval->outfd, nval->line);
		}
	    }
	}
    }
    PSIlog_log(-1, "dump end\n");
}

void outputMergeInit(void)
{
    int i;
    char npsize[50];
    char *envstr;

    if ((envstr = getenv("PSI_MERGETMOUT"))) {
	maxMergeWait = atoi(envstr);
    }

    if ((envstr = getenv("PSI_MERGEDEPTH"))) {
	maxMergeDepth = atoi(envstr);
    }

    ClientOutBuf = umalloc ((sizeof(*ClientOutBuf) * np), __func__);
    BufInc = umalloc ((sizeof(char*) * np), __func__);

    snprintf(npsize, sizeof(npsize), "[0-%i]", np-1);
    prelen = strlen(npsize);

    /* Init Output Buffer List */
    for (i=0; i<np; i++) {
	ClientOutBuf[i].line = NULL;
	INIT_LIST_HEAD(&ClientOutBuf[i].list);
	BufInc[i] = NULL;
    }

    INIT_LIST_HEAD(&messageCache.list);
}

/**
 * @brief Searches the client structure and builds up a new array
 * of equal lines.
 *
 * @param ClientIdx The id of the first client which buffer should be
 * compared against all other clients.
 *
 * @param saveBuf This array receives the positions of equal lines
 * in the client buffers.
 *
 * @param saveBufInd The id's of the client has equal output to the
 * searched one.
 *
 * @param mcount @doctodo
 *
 * @param val The buffer of the client to compare against.
 *
 * @return No return value.
 */
static void findEqualData(int ClientIdx, struct list_head *saveBuf[np],
			  int saveBufInd[np], int *mcount,
			  OutputBuffers *val)
{
    int dcount = 0;
    int x;
    struct list_head *npos;
    OutputBuffers *nval;

    if (!val || !val->line) {
	return;
    }

    *mcount = 0;
    for(x=0; x < np; x++) {
	/* skip myself or empty list */
	if (x == ClientIdx || list_empty(&ClientOutBuf[x].list)) continue;

	list_for_each(npos, &ClientOutBuf[x].list) {
	    dcount++;
	    /* get data to compare */
	    nval = list_entry(npos, OutputBuffers, list);

	    if (!nval || !nval->line) break;

	    if (val->line == nval->line ) {
		if (val->outfd == nval->outfd) {
		    saveBuf[*mcount] = npos;
		    saveBufInd[*mcount] = x;
		    (*mcount)++;
		    break;
		}
	    }
	    if (maxMergeDepth > 0 && dcount == maxMergeDepth) break;
	}
	dcount = 0;
    }
}

/**
 * @brief Deletes a entry from the client buffer and reduces the
 * counter in the global buffer, or delete the entry from the
 * global buffer, if counter is 0.
 *
 * @param nval The element of the client buffer to delete.
 *
 * @param npos The position of the buffer to delete.
 *
 * @return No return value.
 */
static void delCachedMsg(OutputBuffers *nval, struct list_head *npos)
{
    struct list_head *pos;
    bMessages *val;
    int found = 0;
    val = NULL;
    if (!list_empty(&messageCache.list)) {
	list_for_each(pos, &messageCache.list) {
	    /* get element to compare */
	    val = list_entry(pos, bMessages, list);

	    if (!val || !val->line) break;
	    if (nval->line == val->line) {
		found = 1;
		break;
	    }
	}
    } else {
	PSIlog_log(-1, "%s: list empty: possible error in ouput buffer\n",
		   __func__);
	return;
    }
    if (!found) {
	PSIlog_log(-1, "%s: line not found: possible error in ouput buffer\n",
		   __func__);
	return;
    }
    if (!val) {
	PSIlog_log(-1, "%s: empty result: possible error in ouput buffer\n",
		   __func__);
	return;
    }
    (val->counter)--;
    if (val->counter == 0) {
	free(val->line);
	list_del(pos);
	free(val);
    }
    list_del(npos);
    free(nval);
}

/**
 * @brief Generate a intelligent prefix.
 *
 * @param prefix The buffer which receives the prefix.
 *
 * @param size The size of the buffer.
 *
 * @param mcount Number of clients which should be included
 * in the prefix.
 *
 * @param start The id of the first client.
 *
 * @param saveBufInd A list a client id`s which should be
 * included in the prefix.
 *
 * @return No return value.
 */
static void generatePrefix(char *prefix, int size, int mcount, int start,
			   int saveBufInd[np])
{
    int nextRank, firstRank, z;
    char tmp[40];

    prefix[0] = '\0';
    firstRank = start;
    nextRank = start;

    for (z=0; z < mcount; z++) {
	if (saveBufInd[z] == nextRank+1) {
	    nextRank++;
	    continue;
	}

	if (nextRank == firstRank) {
	    if (strlen(prefix) < 1) {
		snprintf(prefix, size, "%i", firstRank);
	    } else {
		snprintf(tmp, sizeof(tmp), ",%i", firstRank);
		strncat(prefix, tmp, size - strlen(prefix) -1);
	    }
	} else {
	    if (strlen(prefix) < 1) {
		snprintf(prefix, size, "%i-%i", firstRank, nextRank);
	    } else {
		snprintf(tmp, sizeof(tmp), ",%i-%i", firstRank, nextRank);
		strncat(prefix, tmp, size - strlen(prefix) -1);
	    }
	}
	firstRank = saveBufInd[z];
	nextRank = saveBufInd[z];
    }

    if (nextRank == firstRank) {
	if (strlen(prefix) < 1) {
	    snprintf(prefix, size, "%i", firstRank);
	} else {
	    snprintf(tmp, sizeof(tmp), ",%i", firstRank);
	    strncat(prefix, tmp, size - strlen(prefix) -1);
	}
    } else {
	if (strlen(prefix) < 1) {
	    snprintf(prefix, size, "%i-%i", firstRank, nextRank);
	} else {
	    snprintf(tmp, sizeof(tmp), ",%i-%i", firstRank, nextRank);
	    strncat(prefix, tmp, size - strlen(prefix) -1);
	}
    }
}

/**
 * @brief Print a single line with correct formating.
 *
 * @param outfd The file descriptor to write to.
 *
 * @param line The line to print.
 *
 * @param mcount The number of clients which has outputed
 * the same line.
 *
 * @param start The id of the first client.
 *
 * @param saveBufInd A list a client id`s which has outputed
 * the same line.
 *
 * @return No return value.
 */
static void printLine(int outfd, char *line, int mcount, int start,
		      int saveBufInd[np])
{
    char prefix[100];
    int space = 0;

    if (db)
	PSIlog_log(-1, "%s: count:%i, start:%i, line:%s\n", __func__,
		   mcount, start, line);

    generatePrefix(prefix, sizeof(prefix), mcount, start, saveBufInd);
    space = prelen - strlen(prefix) - 2;
    if (space < 0) space = 0;

    switch (outfd) {
    case STDOUT_FILENO:
	PSIlog_stdout(-1, "%*s[%s]: %s", space, "", prefix, line);
	break;
    case STDERR_FILENO:
	PSIlog_stderr(-1, "%*s[%s]: %s", space, "", prefix, line);
	break;
    default:
	PSIlog_log(-1, "%s: unknown outfd %d\n", __func__, outfd);
    }
}

/**
 * @brief Outputs all lines in the Client buffer,
 * till the line is found were all clients outputed
 * the same message.
 *
 * @param client The id of the client to output the line.
 *
 * @param pos The position of the next equal line.
 *
 * @param saveBuf The position of equal lines in the
 * client buffer.
 *
 * @param saveBufInd The id`s of the clients which
 * outputed the same line.
 *
 * @param mcount The number of clients which outputed
 * the same line.
 *
 * @return No return value.
 */
static void outputSingleCMsg(int client, struct list_head *pos,
			     struct list_head *saveBuf[np],
			     int saveBufInd[np], int mcount)
{
    struct list_head *tmppos, *tmpother;
    OutputBuffers *nval, *oval;
    int i, lcount;
    int savelocInd[mcount];

    list_for_each(tmppos, &ClientOutBuf[client].list) {
	lcount = 0;
	if (tmppos == pos) break;

	nval = list_entry(tmppos, OutputBuffers, list);
	if (!nval || !nval->line) break;

	for (i=0; i<=mcount; i++) {
	    if (i == client) continue;
	    list_for_each(tmpother, &ClientOutBuf[i].list) {
		if (tmpother == saveBuf[i]) break;

		oval = list_entry(tmpother, OutputBuffers, list);
		if (!oval || !oval->line) break;
		if (oval->line == nval->line && oval->outfd == nval->outfd) {
		    savelocInd[lcount] = i;
		    lcount++;
		    delCachedMsg(oval, tmpother);
		}
	    }
	}
	printLine(nval->outfd, nval->line, lcount, client, savelocInd);
	delCachedMsg(nval,tmppos);
    }
}

/**
 * @brief Output a cached message which has
 * no new line if we flush all messages.
 *
 * @param client The client to output the
 * half message from.
 *
 * @return No return value.
 */
static void outputHalfMsg(int client)
{
    if (!BufInc[client]) return;

    PSIlog_stderr(-1, "[%i]: %s\n", client, BufInc[client]);
    BufInc[client] = NULL;
}

/**
 * @brief Print all collected output which is already merged,
 * or a timeout is reached.
 *
 * @param flush If set to 1 any output is flushed without
 * waiting for a timeout.
 *
 * @return No return value.
 */
void displayCachedOutput(int flush)
{
    struct list_head *pos;
    OutputBuffers *val, *nval;
    int i, z, mcount = 0;
    struct list_head *saveBuf[np];
    int saveBufInd[np];
    time_t ltime = time(0);
    int countMode = 1;

    /* reset tracking array */
    for (i=0; i<np; i++) saveBuf[i] = NULL;

    for (i=0; i < np; i++) {
	if (list_empty(&ClientOutBuf[i].list)) continue;

	list_for_each(pos, &ClientOutBuf[i].list) {
	    /* get element to compare */
	    val = list_entry(pos, OutputBuffers, list);

	    if (!val || !val->line) break;

	    if (ltime - val->time > maxMergeWait || flush) {
		countMode = 0;
	    } else {
		if (*(val->counter) < np -1) {
		    break;
		}
	    }

	    /* find equal data */
	    findEqualData(i, saveBuf, saveBufInd, &mcount, val);

	    /* output the message if
	     * - we hit the timeout
	     * - we exit and flush all left data
	     * - all ranks have output the same msg
	     */
	    if (mcount == np-1 || countMode == 0) {
		/* output single msg from tracked rank */
		outputSingleCMsg(i, pos, saveBuf, saveBufInd, mcount);

		for (z=0; z < mcount; z++) {
		    /* output single msg from other ranks */
		    outputSingleCMsg(saveBufInd[z], saveBuf[z], saveBuf,
				     saveBufInd, mcount);

		    /* remove already displayed msg from all other ranks */
		    nval = list_entry(saveBuf[z], OutputBuffers, list);
		    delCachedMsg(nval,saveBuf[z]);
		}

		if (mcount != np-1) {
		    printLine(val->outfd, val->line, mcount, i, saveBufInd);
		} else {
		    switch (val->outfd) {
		    case STDOUT_FILENO:
			PSIlog_stdout(-1, "[0-%i]: %s", np-1, val->line);
			break;
		    case STDERR_FILENO:
			PSIlog_stderr(-1, "[0-%i]: %s", np-1, val->line);
			break;
		    default:
			PSIlog_log(-1, "%s: unknown outfd %d\n", __func__,
				   val->outfd);
		    }
		}
		delCachedMsg(val, pos);
	    }
	}
	if (flush) outputHalfMsg(i);
    }
}

/**
 * @brief Calculate a simple hash by summing up all
 * characters.
 *
 * @param line The string to calculate the hash from.
 *
 * @return Returns the calculated hash value.
 */
static int calcHash(char *line)
{
    unsigned int hash, i;

    for (i=0,hash=0; i <strlen(line); i++) {
	hash += line[i];
    }
    return hash;
}

/**
 * @brief Checks if a message is already saved in the global
 * msg cache.
 *
 * @param msg The message which should be checked.
 *
 * @return If the message is found a pointer to that message
 * is returned. If not found NULL is returned.
 */
static bMessages *isSaved(char *msg, int hash)
{
    struct list_head *pos;
    bMessages *val;

    if (!list_empty(&messageCache.list)) {
	list_for_each(pos, &messageCache.list) {
	    /* get element to compare */
	    val = list_entry(pos, bMessages, list);

	    if (!val || !val->line) break;
	    if (val->hash == hash && (!strcmp(val->line, msg))) {
		return val;
	    }
	}
    }
    return NULL;
}

/**
 * @brief Cache the received output msg.
 *
 * @param msg The received msg which holds
 * the buffer to cache.
 *
 * @param outfd The file descriptor to write the msgs to.
 *
 * @return No return value.
 */
void cacheOutput(PSLog_Msg_t *msg, int outfd)
{
    size_t count = msg->header.len - PSLog_headerSize;
    int sender = msg->sender;
    OutputBuffers *newMsg = NULL;
    OutputBuffers *ClientBuf = &ClientOutBuf[sender];
    bMessages *globalMsg = NULL;
    char *buf, *bufmem;
    int len;
    int hash;

    bufmem = umalloc((count +1), __func__);
    strncpy(bufmem, msg->buf, count);
    bufmem[count] = '\0';
    buf = bufmem;

    /* don't try to merge output from special ranks e.g. service processes */
    if (sender < 0) {
	switch (outfd) {
	case STDOUT_FILENO:
	    PSIlog_stdout(-1, "[%i]: %s", sender, buf);
	    break;
	case STDERR_FILENO:
	    PSIlog_stderr(-1, "[%i]: %s", sender, buf);
	    break;
	default:
	    PSIlog_log(-1, "%s: unknown outfd %d\n", __func__, outfd);
	}
	return;
    }

    while (count>0 && strlen(buf) >0) {
	char *nl, *savep;

	len = strlen(buf);
	nl  = strchr(buf, '\n');
	if (nl) nl++; /* Thus nl points behind the newline */

	/* no newline -> save to tmp buffer */
	if (!nl) {
	    if (!BufInc[sender]) {
		if (db) PSIlog_log(-1, "sender:%i no newline ->newbuf :%s\n",
				   sender, buf);
		BufInc[sender] = umalloc((len +1), __func__);

		strncpy(BufInc[sender], buf, len);
		BufInc[sender][len] = '\0';
	    } else {
		/* buffer is used, append the msg */
		int leninc = strlen(BufInc[sender]);
		if (db) PSIlog_log(-1, "sender:%i no newline ->append :%s\n",
				   sender, buf);
		BufInc[sender] = urealloc(BufInc[sender], leninc + len +1,
					  __func__);
		strncat(BufInc[sender], buf, len);
		BufInc[sender][len + leninc] = '\0';
	    }
	    break;
	}

	/* complete msg with newline (\n) */
	len = strlen(buf) - strlen(nl);
	if (BufInc[sender]) {
	    int leninc = strlen(BufInc[sender]);
	    BufInc[sender] = urealloc(BufInc[sender], leninc + len +1,
				     __func__);
	    strncat(BufInc[sender], buf, len);
	    savep = BufInc[sender];
	    savep[leninc + len] = '\0';
	    BufInc[sender] = NULL;
	} else {
	    savep = umalloc((len +1), __func__);
	    strncpy(savep, buf, len);
	    savep[len] = '\0';
	}

	if (db)
	    PSIlog_log(-1, "string to global sender:%i outfd:%i savep:' %s'\n",
		       sender, outfd, savep);

	/* setup new client list item */
	newMsg = (OutputBuffers *)umalloc(sizeof(OutputBuffers), __func__);
	newMsg->time = time(0);
	newMsg->outfd = outfd;

	/* check if already in global buffer */
	hash = calcHash(savep);

	if ((globalMsg = isSaved(savep, hash))) {
	    /* string is already in global msg cache, just save ref to it */
	    (globalMsg->counter)++;
	    newMsg->line = globalMsg->line;
	    newMsg->counter = &(globalMsg->counter);

	    if (db)
		PSIlog_log(-1,
			   "pointer to matrix count:%i hash:%i savep:'%s'\n",
			   globalMsg->counter, globalMsg->hash, savep);
	    free(savep);
	} else {
	    /* new message, save it to global msg cache */
	    globalMsg = (bMessages *)umalloc(sizeof(bMessages), __func__);
	    globalMsg->line = savep;
	    globalMsg->hash = hash;
	    globalMsg->counter = 1;
	    list_add_tail(&(globalMsg->list), &messageCache.list);

	    newMsg->line = savep;
	    newMsg->counter = &(globalMsg->counter);

	    if (db)
		PSIlog_log(-1,
			   "string to matrix count:%i hash:%i savep:'%s'\n",
			   globalMsg->counter, globalMsg->hash, savep);
	}

	/* save the message to the client list */
	list_add_tail(&(newMsg->list), &ClientBuf->list);

	/* goto next line */
	count -= len;
	buf = nl;
    }
    free(bufmem);

    /* just for debugging */
    if (db) {
	dumpGlobalBuffer();
	dumpClientBuffer();
    }
}
