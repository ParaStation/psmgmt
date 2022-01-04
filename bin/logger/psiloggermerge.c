/*
 * ParaStation
 *
 * Copyright (C) 2007-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psiloggermerge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "psprotocol.h"
#include "list.h"
#include "linenoise.h"
#include "psilogger.h"

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
 * Structure for temp buffer
 */
typedef struct {
    time_t time[3];
    char *line[3];
} TempBuffers;

/**
 * Maximum/Current number of processes in this job.
 */
extern int usize;
extern int np;

/**
 * Len of the prefix if all ranks do the same output.
 */
static int prelen;

/**
 * Structure for each client which holds the pointers to the
 * cached messages (points into messageCache).
 */
static OutputBuffers *clientOutBuf;

/**
 * Structure for each client which holds incomplete
 * messages.
 */
static TempBuffers *clientTmpBuf;

/**
 * All received complete messages are cached in this structure.
 */
static bMessages messageCache;

/**
 * All received incomplete messages are cached in this structure.
 */
static bMessages msgTmpCache;

/**
 * Set the maxima depth (rows) for searching for equal
 * output lines in the cache.
 *
 * in the matrix. -1 for infinit depth.
 */
static int maxMergeDepth = 200;

/**
 * Set the time in seconds who long a received output msg
 * will maximal be hold back.
 */
static int maxMergeWait = 2;

/** Enable debbuging messages. */
static bool db = false;

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
 * @brief Dump global buffer.
 *
 * Just for debbuging purpose, print the global message buffer.
 *
 * @return No return value.
 */
static void dumpGlobalBuffer(void)
{
    struct list_head *npos;

    PSIlog_stderr(-1, "\n");
    PSIlog_log(-1, "Dumping output buffer:\n");
    list_for_each(npos, &messageCache.list) {
	bMessages *nval = list_entry(npos, bMessages, list);
	if (nval->line) {
	    PSIlog_log(-1, "counter:%i hash:%i line:%s", nval->counter,
		       nval->hash, nval->line);
	}
    }
    PSIlog_log(-1, "dump end\n");
}

/**
 * @brief Dump client output buffer.
 *
 * Just for debbuging purpose, print the client message buffers.
 *
 * @return No return value.
 */
static void dumpClientBuffer(void)
{
    int x;

    PSIlog_stderr(-1, "\n");
    PSIlog_log(-1, "Dumping output buffer:\n");
    for(x=0; x< np; x++) {
	struct list_head *npos;
	list_for_each(npos, &clientOutBuf[x].list) {
	    OutputBuffers *nval = list_entry(npos, OutputBuffers, list);
	    if (nval->line) {
		PSIlog_log(-1, "client:%i outfd:%d line:%s", x, nval->outfd,
			   nval->line);
	    }
	}
    }
    PSIlog_log(-1, "dump end\n");
}

/**
 * @brief Init the merge layer.
 *
 * This init function have to be called before any other
 * merge function.
 *
 * @return No return value.
 */
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

    clientOutBuf = umalloc((sizeof(*clientOutBuf) * usize), __func__);
    clientTmpBuf = umalloc((sizeof(*clientTmpBuf) * usize), __func__);

    snprintf(npsize, sizeof(npsize), "[0-%i]", usize-1);
    prelen = strlen(npsize);

    /* Init Output Buffer List */
    for (i=0; i<usize; i++) {
	clientOutBuf[i].line = NULL;
	clientTmpBuf[i].line[0] = NULL;
	clientTmpBuf[i].line[1] = NULL;
	clientTmpBuf[i].line[2] = NULL;
	INIT_LIST_HEAD(&clientOutBuf[i].list);
    }

    INIT_LIST_HEAD(&messageCache.list);
    INIT_LIST_HEAD(&msgTmpCache.list);
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
 * @param mcount Upon return number of identical lines found.
 *
 * @param val The buffer of the client to compare against.
 *
 * @return No return value.
 */
static void findEqualData(int ClientIdx, struct list_head *saveBuf[np],
			  int saveBufInd[np], int *mcount, OutputBuffers *val)
{
    int x;

    if (!val || !val->line) return;

    *mcount = 0;
    for(x=0; x<np; x++) {
	struct list_head *npos;
	int dcount = 0;

	/* skip myself */
	if (x == ClientIdx) continue;

	list_for_each(npos, &clientOutBuf[x].list) {
	    OutputBuffers *nval = list_entry(npos, OutputBuffers, list);

	    dcount++;

	    if (!nval->line) break;

	    if (val->line == nval->line && val->outfd == nval->outfd) {
		saveBuf[*mcount] = npos;
		saveBufInd[*mcount] = x;
		(*mcount)++;
		break;
	    }
	    if (maxMergeDepth > 0 && dcount == maxMergeDepth) break;
	}
    }
}

/**
 * @brief Delete a entry form the client buffer.
 *
 * Delete an entry from the client buffer and reduce the counter in
 * the global buffer, or delete the entry from the global buffer if
 * counter is 0.
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
    bMessages *val = NULL;
    bool found = false;

    if (list_empty(&messageCache.list)) {
	PSIlog_log(-1, "%s: list empty: possible error in ouput buffer\n",
		   __func__);
	return;
    }

    list_for_each(pos, &messageCache.list) {
	val = list_entry(pos, bMessages, list);

	if (!val->line) break;
	if (nval->line == val->line) {
	    found = true;
	    break;
	}
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
    if (!val->counter) {
	free(val->line);
	list_del(pos);
	free(val);
    }
    list_del(npos);
    free(nval);
}

/**
 * @brief Generate an intelligent prefix.
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

    if(np==1) {
	 snprintf(tmp, sizeof(tmp), "0");
	 strncat(prefix, tmp, size - strlen(prefix) -1);
	 return;
    }

    for (z=0; z<mcount; z++) {
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

    if (db) PSIlog_log(-1, "%s: count:%i, start:%i, line:%s\n", __func__,
		   mcount, start, line);

    generatePrefix(prefix, sizeof(prefix), mcount, start, saveBufInd);
    space = prelen - strlen(prefix) - 2;
    if (space < 0) space = 0;

    if (enableGDB) {
	if (!strncmp(line, "(gdb)\r", 6)) {
	    linenoiseSetPrompt(GDBprompt);
	    linenoiseForcedUpdateDisplay();
	    GDBcmdEcho = false;
	    return;
	}
	if (GDBcmdEcho) {
	    GDBcmdEcho = false;
	    return;
	}
    }

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
    list_t *tmppos, *savepos;
    list_for_each_safe(tmppos, savepos, &clientOutBuf[client].list) {
	OutputBuffers *nval = list_entry(tmppos, OutputBuffers, list);
	int lcount = 0, i;
	int savelocInd[mcount+1];

	if (tmppos == pos || !nval->line) break;

	for (i=0; i<=mcount; i++) {
	    if (i == client) continue;
	    list_t *tmpother, *saveother;
	    list_for_each_safe(tmpother, saveother, &clientOutBuf[i].list) {
		OutputBuffers *oval = list_entry(tmpother, OutputBuffers, list);

		if (tmpother == saveBuf[i] || !oval->line) break;

		if (oval->line == nval->line && oval->outfd == nval->outfd) {
		    savelocInd[lcount] = i;
		    lcount++;
		    delCachedMsg(oval, tmpother);
		    break;
		}
	    }
	}
	printLine(nval->outfd, nval->line, lcount, client, savelocInd);
	delCachedMsg(nval,tmppos);
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

    for (i=0,hash=0; i<strlen(line); i++) {
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
static bMessages *isSaved(bMessages *msgCache, char *msg, int hash)
{
    struct list_head *pos;

    if (!msgCache || !msg) {
	PSIlog_log(-1, "%s: invalid msg or msgCache\n", __func__);
	return NULL;
    }

    list_for_each(pos, &msgCache->list) {
	bMessages *val = list_entry(pos, bMessages, list);

	if (!val->line) break;
	if (val->hash == hash && !strcmp(val->line, msg)) return val;
    }
    return NULL;
}

/**
 * @brief Concatenate two strings
 *
 * Concatenate the two character strings @a str1 and @a str2 and store
 * the result in a new string. The function returns a pointer to the
 * newly allocated string. Memory for the new string is obtained with
 * @ref umalloc(), and must be freed with free(). The length of the
 * string @a str2 to be appended must be given in @a len.
 *
 * @param str1 First string
 *
 * @param str2 Second string
 *
 * @param len Length of @a str2
 *
 * @param func Name of the calling function to be passed to @ref umalloc()
 *
 * @return Returns a pointer to the new string containing the
 * concatenated content of @a str1 and @a str2
 */
static char *strndupcat(char *str1, char *str2, size_t len, const char *func)
{
    size_t len1 = strlen(str1);
    char *savep = umalloc((len1 + len + 1), func);
    if (savep) {
	memcpy(savep, str1, len1);
	memcpy(savep + len1, str2, len);
	savep[len1 + len] = '\0';
    }

    return savep;
}

/**
 * @brief Save msg in temporarly buffer.
 *
 * @param sender The sender of the message.
 *
 * @param newmsg The buffer which holds the message.
 *
 * @param len The len of the message.
 */
static void appendTmpBuffer(int sender, char *newmsg, size_t len, int outfd)
{
    bMessages *globalMsg = NULL;
    TempBuffers *tmpBuf = &clientTmpBuf[sender];
    char *tmpLine = tmpBuf->line[outfd];
    char *savebuf = NULL;
    int hash;

    if (outfd > 2) {
	PSIlog_log(-1, "%s: unsupported outfd %d\n", __func__, outfd);
	return;
    }

    if (!newmsg || len > strlen(newmsg)) {
	PSIlog_log(-1, "%s: error in buffer\n", __func__);
	return;
    }

    if (!tmpLine) {
	if (db) PSIlog_log(-1, "sender:%i no '\\n' ->newbuf :%s\n",
			   sender, newmsg);
	tmpBuf->time[outfd] = time(0);
	savebuf = strndup(newmsg, len);
    } else {
	if (db) PSIlog_log(-1, "sender:%i no '\\n' ->append :%s\n",
			   sender, newmsg);
	savebuf = strndupcat(tmpLine, newmsg, len, __func__);
    }

    if (!savebuf) {
	PSIlog_log(-1, "%s: invalid message to save\n", __func__);
	return;
    }

    /* insert into tmp msg cache */
    hash = calcHash(savebuf);
    if ((globalMsg = isSaved(&msgTmpCache, savebuf, hash))) {
	(globalMsg->counter)++;
	free(savebuf);
    } else {
	globalMsg = (bMessages *)umalloc(sizeof(bMessages), __func__);
	globalMsg->line = savebuf;
	globalMsg->hash = hash;
	globalMsg->counter = 1;
	list_add_tail(&(globalMsg->list), &msgTmpCache.list);
    }
    tmpBuf->line[outfd] = globalMsg->line;
}

/**
 * @brief Delete a msg from the tmp cache.
 *
 * @param tmpLine The pointer to the msg to delete.
 *
 * @return No return value.
 */
static void delCachedTmpMsg(char *tmpLine)
{
    struct list_head *pos;
    bMessages *val = NULL;
    bool found = false;

    if (list_empty(&msgTmpCache.list)) {
	PSIlog_log(-1, "%s: list empty: possible error in tmp msg cache\n",
		   __func__);
	return;
    }

    list_for_each(pos, &msgTmpCache.list) {
	val = list_entry(pos, bMessages, list);

	if (!val->line) break;
	if (val->line == tmpLine) {
	    found = true;
	    break;
	}
    }

    if (!found) {
	PSIlog_log(-1, "%s: line not found: possible error in tmp msg cache\n",
		   __func__);
	return;
    }

    if (!val) {
	PSIlog_log(-1, "%s: empty result: possible error in tmp msg cache\n",
		   __func__);
	return;
    }

    (val->counter)--;
    if (val->counter == 0) {
	free(val->line);
	list_del(pos);
	free(val);
    }
}

/**
 * @brief Save msg in rank specific output buffer.
 *
 * @param sender The sender of the message.
 *
 * @param buf The buffer which holds the message.
 *
 * @param len The len of the message.
 *
 * @param outfd The file descriptor for output the msg.
 */
static void insertOutputBuffer(int sender, char *buf, size_t len, int outfd)
{
    OutputBuffers *newMsg = NULL;
    OutputBuffers *ClientBuf = &clientOutBuf[sender];
    TempBuffers *tmpBuf = &clientTmpBuf[sender];
    char *tmpLine = tmpBuf->line[outfd];
    bMessages *globalMsg = NULL;
    char *savep = NULL;
    int hash;

    if (!buf || len > strlen(buf)) {
	PSIlog_log(-1, "%s: error in buffer\n", __func__);
	return;
    }

    if (!tmpLine) {
	savep = strndup(buf, len);
    } else {
	savep = strndupcat(tmpLine, buf, len, __func__);
	delCachedTmpMsg(tmpLine);
	tmpBuf->line[outfd] = NULL;
    }
    if (!savep) {
	PSIlog_log(-1, "%s: invalid message to save\n", __func__);
	return;
    }

    /* Shall output lines be scanned for Valgrind PID patterns? */
    if (useValgrind) {
	/* Yes: replace every first occurrence of '==12345==' by '=========' */
	char *ptr1, *ptr2, *ptr3;

	ptr1 = strstr(savep, "==");
	if (ptr1) {
	    ptr2 = strstr(ptr1+2, "==");
	    if (ptr2) {
		if (strtol(ptr1+2, &ptr3, 10) && (ptr3 == ptr2)) {
		    while(ptr1 != ptr2) {
			(*ptr1) = '=';
			ptr1++;
		    }
		}
	    }
	}
    }


    if (db)
	PSIlog_log(-1, "string to global sender:%i outfd:%i savep:' %s'\n",
		   sender, outfd, savep);

    /* check if already in global buffer */
    hash = calcHash(savep);

    if ((globalMsg = isSaved(&messageCache, savep, hash))) {
	/* string is already in global msg cache, just save ref to it */
	(globalMsg->counter)++;

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

	if (db)
	    PSIlog_log(-1,
		       "string to matrix count:%i hash:%i savep:'%s'\n",
		       globalMsg->counter, globalMsg->hash, savep);
    }

    /* setup new client list item */
    newMsg = (OutputBuffers *)umalloc(sizeof(OutputBuffers), __func__);
    newMsg->time = time(0);
    newMsg->outfd = outfd;
    newMsg->line = globalMsg->line;
    newMsg->counter = &(globalMsg->counter);

    /* save the message to the client list */
    list_add_tail(&(newMsg->list), &ClientBuf->list);
}

/**
 * @brief Move all msg in tmp cache to client cache.
 *
 * @return No return value.
 */
static void moveTmpToClientBuf(void)
{
    TempBuffers *tmpBuf;
    int i,z;
    char slash[] = "\\\n";

    for (i=0; i<np; i++) {
	tmpBuf = &clientTmpBuf[i];
	for (z=0; z<3; z++) {
	    if (tmpBuf->line[z]) {
		insertOutputBuffer(i, slash, strlen(slash), z);
	    }
	}
   }
}

void cacheOutput(PSLog_Msg_t *msg, int outfd)
{
    size_t count = msg->header.len - PSLog_headerSize;
    int sender = msg->sender;
    char *bufmem;
    int len;

    bufmem = umalloc(count + 1, __func__);
    strncpy(bufmem, msg->buf, count);
    bufmem[count] = '\0';

    /* don't try to merge output from special ranks e.g. service processes */
    if (sender < 0) {
	switch (outfd) {
	case STDOUT_FILENO:
	    PSIlog_stdout(-1, "[%i]: %s", sender, bufmem);
	    break;
	case STDERR_FILENO:
	    PSIlog_stderr(-1, "[%i]: %s", sender, bufmem);
	    break;
	default:
	    PSIlog_log(-1, "%s: unknown outfd %d\n", __func__, outfd);
	}
	free(bufmem);
	return;
    }

    if (sender >= np) {
	 /* extend the number of current processes: */
	 np = sender + 1;
    }

    char *buf = bufmem;
    while (count>0 && strlen(buf) >0) {
	char *nl;

	len = strlen(buf);
	nl  = strchr(buf, '\n');
	if (nl) nl++; /* Thus nl points behind the newline */

	/* no newline -> save whole msg in tmp buffer */
	if (!nl) {
	    appendTmpBuffer(sender, buf, len, outfd);
	    break;
	}

	/* complete msg with newline (\n) */
	len = strlen(buf) - strlen(nl);
	insertOutputBuffer(sender, buf, len, outfd);

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

void displayCachedOutput(bool flush)
{
    int i, z, mcount = 0;
    struct list_head *saveBuf[np];
    int saveBufInd[np];
    time_t ltime = time(0);
    bool countMode = true;

    /* reset tracking array */
    for (i=0; i<np; i++) saveBuf[i] = NULL;

    /* add all the leftovers in tmp buffer to normal buffer */
    if (flush) moveTmpToClientBuf();

    for (i=0; i<np; i++) {
	list_t *pos, *tmp;
	list_for_each_safe(pos, tmp, &clientOutBuf[i].list) {
	    OutputBuffers *val = list_entry(pos, OutputBuffers, list);

	    if (!val->line) break;

	    if (ltime - val->time > maxMergeWait || flush) {
		countMode = false;
	    } else if (*(val->counter) < np -1) {
		break;
	    }

	    /* find equal data */
	    findEqualData(i, saveBuf, saveBufInd, &mcount, val);

	    /* output the message if
	     * - we hit the timeout
	     * - we exit and flush all left data
	     * - all ranks have output the same msg
	     */
	    if (countMode && mcount != np-1) continue;

	    /* output single msg from tracked rank */
	    outputSingleCMsg(i, pos, saveBuf, saveBufInd, mcount);

	    for (z=0; z<mcount; z++) {
		/* output single msg from other ranks */
		outputSingleCMsg(saveBufInd[z], saveBuf[z], saveBuf,
				 saveBufInd, mcount);

		/* remove already displayed msg from all other ranks */
		OutputBuffers *nval = list_entry(saveBuf[z],
						 OutputBuffers, list);
		delCachedMsg(nval, saveBuf[z]);
	    }

	    if (mcount != np-1) {
		printLine(val->outfd, val->line, mcount, i, saveBufInd);
	    } else {
		if (enableGDB && !strncmp(val->line, "(gdb)\r", 6)) {
		    linenoiseSetPrompt(GDBprompt);
		    linenoiseForcedUpdateDisplay();
		    GDBcmdEcho = false;
		} else if (enableGDB && GDBcmdEcho) {
		    GDBcmdEcho = false;
		} else {
		    switch (val->outfd) {
		    case STDOUT_FILENO:
			if (np==1) {
			    PSIlog_stdout(-1, "%*s[0]: %s", prelen - 3, "",
					  val->line);
			} else {
			    PSIlog_stdout(-1, "%*s[0-%i]: %s", prelen - 5, "",
					  np-1, val->line);
			}
			break;
		    case STDERR_FILENO:
			if (np==1) {
			    PSIlog_stderr(-1, "%*s[0]: %s", prelen - 3, "",
					  val->line);
			} else {
			    PSIlog_stderr(-1, "%*s[0-%i]: %s", prelen - 5, "",
					  np-1, val->line);
			}
			break;
		    default:
			PSIlog_log(-1, "%s: unknown outfd %d\n", __func__,
				   val->outfd);
		    }
		}
	    }

	    delCachedMsg(val, pos);
	}
    }
}
