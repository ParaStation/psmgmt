/*
 *               ParaStation
 *
 * Copyright (C) 2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 *
 */
/**
 * \file
 * psiloggermerge.c: ParaStation functions for output merging 
 *
 * $Id$ 
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "list.h"
#include "pslog.h"
#include "psiloggermerge.h"

/**
 * Structure for each client which holds the pointers to the
 * cached message.
 */
typedef struct {
    time_t time;
    char *line;
    struct list_head list;
    FILE* outfp;
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

/** The actual size of #OutputBuffers. */
extern int maxClients;

/**
 * Number of maximal processes in this job.
 */
extern int np;

/**
 * Len of the prefix if all ranks do the same output.
 */
int prelen;

/**
 * Structure for each client which holds the pointers to the
 * cached message.
 */
OutputBuffers *ClientOutBuf;

/**
 * List of received output msgs (the actual buffer).
 */
bMessages bufGlobal;

/**
 * Buffer for incomplete lines.
 */
char **BufInc;

/**
 * Set the maxima depth (rows) for searching for equal
 * output lines in the cache.
 *
 * in the matrix. -1 for infinit depth. 
 */
int maxMergeDepth = 300; 

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
	fprintf(stderr, "PSIlogger: %s: malloc() failed.\n", func);
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
	fprintf(stderr, "PSIlogger: %s: realloc) failed.\n", func);
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

    fprintf(stdout, "\nDumping output buffer:\n");
    if (!list_empty(&bufGlobal.list)) {
	list_for_each(npos, &bufGlobal.list) {
	    nval = list_entry(npos, bMessages, list);	 
	    if (!nval) {
		break;
	    }
	    if (nval->line) {
		fprintf(stdout, "counter:%i hash:%i line:%s", nval->counter, 
				nval->hash, nval->line);
	    }
	}
    }
    fprintf(stdout, "dump end\n\n");
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

    fprintf(stdout, "\nDumping client buffer:\n");
    
    for(x=0; x< maxClients; x++) {
	
	if (!list_empty(&ClientOutBuf[x].list)) {
	    list_for_each(npos, &ClientOutBuf[x].list) {
		/* get data to compare */
		nval = list_entry(npos, OutputBuffers, list);	 
		
		if (!nval) {
		    break;
		}
		if (nval->line) {
		    fprintf(stdout, "client:%i outfd:%i line:%s", x, (int) 
				     nval->outfp, nval->line);
		}
	    }
	}
    }
    fprintf(stdout, "dump end\n\n");
}

/**
 * @brief Init the merge structures/functions of the logger. 
 * This function must be called before any other merge function.
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

    ClientOutBuf = umalloc ((sizeof(*ClientOutBuf) * maxClients), __func__);
    BufInc = umalloc ((sizeof(char*) * maxClients), __func__);

    snprintf(npsize, sizeof(npsize), "[0-%i]", np -1);
    prelen = strlen(npsize);
  
    /* Init Output Buffer List */
    for (i=0; i<maxClients; i++) { 
	ClientOutBuf[i].line = NULL;
	INIT_LIST_HEAD(&ClientOutBuf[i].list);
	BufInc[i] = NULL;
    }

    INIT_LIST_HEAD(&bufGlobal.list);
}

/**
 * @brief If more clients are connected than maxClients,
 * the caches have to be reallocated so all new clients can be
 * handled.
 *
 * @param newSize The new size of the next max client.
 *
 * @return No return value.
 */
void reallocClientOutBuf(int newSize)
{
    int i;
    OutputBuffers *OutBufNew;
    
    if (db) {
	dumpGlobalBuffer();
	dumpClientBuffer();
	fprintf(stderr, "%s: realloc buffers, newSize:%i maxClients:%i \n", 
			    __func__, newSize, maxClients);
    }

    OutBufNew = umalloc(sizeof(*OutBufNew) * newSize, __func__);
    BufInc = urealloc(BufInc, sizeof(char*) * newSize, __func__);

    for (i=0; i<newSize; i++) {
	INIT_LIST_HEAD(&OutBufNew[i].list);
    }

    for (i=0; i<maxClients; i++) {
	list_splice(&ClientOutBuf[i].list, &OutBufNew[i].list);
    }

    for (i=maxClients; i<newSize; i++) {
	OutBufNew[i].line = NULL;
	BufInc[i] = NULL;
    }

    ClientOutBuf = OutBufNew;
    maxClients = newSize;
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
 * @param OutputBuffers The buffer of the client to compare against.
 *
 * @return No return value.
 */
static void findEqualData(int ClientIdx, struct list_head *saveBuf[maxClients], 
			  int saveBufInd[maxClients], int *mcount, 
			  OutputBuffers *val)
{
    int dcount = 0;
    int x;
    struct list_head *npos;
    OutputBuffers *nval;

    *mcount = 0;
    for(x=0; x< maxClients; x++) {
	
	/* skip myself */
	if (x == ClientIdx) continue;
	
	if (!list_empty(&ClientOutBuf[x].list)) {
	    list_for_each(npos, &ClientOutBuf[x].list) {
		dcount++;
		/* get data to compare */
		nval = list_entry(npos, OutputBuffers, list);	 
		
		if (!val || !nval || !val->line || !nval->line) {
		    //fprintf(stderr, "Breaking search for %s, possible error?\n",
		    //val->line);
		    break;
		}
		if (val->line == nval->line ) {
		    if (val->outfp == nval->outfp) {
			//printf("client: %i np:%i mcount:%i\n", x, np, *mcount);
			saveBuf[*mcount] = npos;
			saveBufInd[*mcount] = x; 
			(*mcount)++;
			break;
		    } else {
			//fprintf(stderr, "same line diff outfp:val->line, val:"
			//"%i nval: %i\n", val->outfp, nval->outfp);
		    }
		}
		if (maxMergeDepth > 0 && dcount == maxMergeDepth) break;
	    }
	    dcount = 0;
	}
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
    if (!list_empty(&bufGlobal.list)) {
	list_for_each(pos, &bufGlobal.list) {
	    /* get element to compare */
	    val = list_entry(pos, bMessages, list); 
	    
	    if(!val || !val->line) {
		break;
	    }
	    if (nval->line == val->line) {
		found = 1;
		break;
	    }
	}
    } else {
	fprintf(stderr, "%s: list empty: possible error in ouput buffer\n", 
		__func__);
	return;
    }
    if (!found) {
	fprintf(stderr, "%s: line not found: possible error in ouput buffer\n",
		__func__);
	return;
    }
    if (!val) {
	fprintf(stderr, "%s: empty result: possible error in ouput buffer\n", 
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
			   int saveBufInd[maxClients])
{
    int nextRank, firstRank, z;	   
    char tmp[40];
    
    prefix[0] = '\0'; 
    firstRank = start; 
    nextRank = start; 

    for (z=0; z < mcount; z++) {
	if (saveBufInd[z] == nextRank+1) {
	    nextRank++;
	} else {
	    if (nextRank == firstRank) {
		if(strlen(prefix) < 1) {
		    snprintf(prefix, size, "%i", firstRank); 
		} else {
		    snprintf(tmp, sizeof(tmp), ",%i", firstRank);
		    strncat(prefix, tmp, size - strlen(prefix) -1); 
		}
	    } else {
		if(strlen(prefix) < 1) {
		    snprintf(prefix, size, "%i-%i", firstRank, nextRank); 
		} else {
		    snprintf(tmp, sizeof(tmp), ",%i-%i", firstRank, nextRank);
		    strncat(prefix, tmp, size - strlen(prefix) -1); 
		}
	    }
	    firstRank = saveBufInd[z];
	    nextRank = saveBufInd[z];
	}
    }
   
    if (nextRank == firstRank) {
	if(strlen(prefix) < 1) {
	    snprintf(prefix, size, "%i", firstRank); 
	} else {
	    snprintf(tmp, sizeof(tmp), ",%i", firstRank);
	    strncat(prefix, tmp, size - strlen(prefix) -1); 
	}
    } else {
	if(strlen(prefix) < 1) {
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
 * @param outfp The file descriptor to write to.
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
static void printLine(FILE *outfp, char *line, int mcount, int start, 
		      int saveBufInd[maxClients])
{
    char format[50];
    char prefix[100]; 
    int space = 0;
    
    if (db) fprintf(stderr, "%s: count:%i, start:%i, line:%s\n", __func__, 
		     mcount, start, line);

    generatePrefix(prefix, sizeof(prefix), mcount, start, saveBufInd);
    space = prelen - strlen(prefix) - 2;
    if (space >0) {
	snprintf(format, sizeof(format), "%%%is[%%s]: %%s", space);  
	fprintf(outfp, format, " ", prefix, line);
    } else {
	snprintf(format, sizeof(format), "[%%s]: %%s");  
	fprintf(outfp, format, prefix, line);
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
			     struct list_head *saveBuf[maxClients], 
			     int saveBufInd[maxClients], int mcount)
{
    struct list_head *tmppos, *tmpother;
    OutputBuffers *nval, *oval;
    int i, lcount;
    int savelocInd[mcount];

    list_for_each(tmppos, &ClientOutBuf[client].list) {
	lcount = 0;
	if (tmppos != pos) {
	    nval = list_entry(tmppos, OutputBuffers, list);
	    if (!nval || !nval->line) break;
	    for (i=0; i<=mcount; i++) {
		if (i == client) continue;
		list_for_each(tmpother, &ClientOutBuf[i].list) {
		    if (tmpother != saveBuf[i]) {
			oval = list_entry(tmpother, OutputBuffers, list);
			if (!oval || !oval->line) break;
			if (oval->line == nval->line && 
			    oval->outfp == nval->outfp) {
			    savelocInd[lcount] = i;
			    lcount++;
			    delCachedMsg(oval, tmpother);
			}
		    } else {
			break;
		    }
		}
	    }
	    printLine(nval->outfp, nval->line, lcount, client, savelocInd);
	    delCachedMsg(nval,tmppos);
	} else {
	    break;
	}
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
    if (BufInc[client] != NULL)  {   
       fprintf(stderr, "[%i]: %s\n", client, BufInc[client]); 
       BufInc[client] = NULL;
    }
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
    int u, i, z, mcount = 0;
    struct list_head *saveBuf[maxClients];
    int saveBufInd[maxClients];
    time_t ltime = time(0); 
    char prefix[100]; 
    
    /* reset tracking array */
    for (u=0; u<maxClients; u++) saveBuf[u] = NULL; 

    for (i=0; i < maxClients; i++) {
	if (!list_empty(&ClientOutBuf[i].list)) {
	    list_for_each(pos, &ClientOutBuf[i].list) {
		
		/* get element to compare */
		val = list_entry(pos, OutputBuffers, list); 

		if(!val || !val->line) {
		    break;
		}

		/* find equal data */
		findEqualData(i, saveBuf, saveBufInd, &mcount, val);

		if (mcount == np -1 || ltime - val->time > maxMergeWait || 
		    flush ) {
		    
		    /*
		    fprintf(stderr, "Analyse np:%i mcount:%i timediff:%i flush:%i "
		    "val:%s\n", np, mcount, (int )(ltime - val->time), flush, 
		    val->line);*/
		    
		    /* output single msg from tracked rank */
		    outputSingleCMsg(i, pos, saveBuf, saveBufInd, mcount);
	     
		    for (z=0; z < mcount; z++) {
			/* ouput single msg from other ranks */
			outputSingleCMsg(saveBufInd[z], saveBuf[z], saveBuf, 
					 saveBufInd, mcount);
			
			/* remove already displayed msg from all other ranks */
			nval = list_entry(saveBuf[z], OutputBuffers, list);
			delCachedMsg(nval,saveBuf[z]);
		    }
		    
		    if (mcount != np -1) {
			printLine(val->outfp, val->line, mcount, i, saveBufInd);
		    } else {
			snprintf(prefix, sizeof(prefix), "[0-%i]", np -1);
			fprintf(val->outfp, "%s: %s", prefix, val->line);
		    }
		    delCachedMsg(val, pos);
		} 
	    }
	    if (flush) outputHalfMsg(i);
	}
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
    
    if (!list_empty(&bufGlobal.list)) {
	list_for_each(pos, &bufGlobal.list) {
	    /* get element to compare */
	    val = list_entry(pos, bMessages, list); 
	    
	    if(!val || !val->line) {
		break;
	    }
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
void cacheOutput(PSLog_Msg_t msg, int outfd)
{
    size_t count = msg.header.len - PSLog_headerSize;
    int sender = msg.sender;
    OutputBuffers *newMsg = NULL;
    OutputBuffers *ClientBuf = &ClientOutBuf[sender];
    bMessages *globalMsg = NULL;
    char *buf, *bufmem;
    int len;
    int hash;
   
    bufmem = umalloc((count +1), __func__);
    strncpy(bufmem, msg.buf, count);
    bufmem[count] = '\0';
    buf = bufmem;

    while (count>0 && strlen(buf) >0) {
	char *nl, *savep;
	
	len = strlen(buf);
	nl  = strchr(buf, '\n');
	if (nl) nl++; /* Thus nl points behind the newline */

	/* no newline -> save to tmp buffer */
	if (!nl) {
	    if (!BufInc[sender]) {
		if (db) fprintf(stderr, "sender:%i no newline ->newbuf :%s\n", 
				sender, buf);
		BufInc[sender] = umalloc((len +1), __func__);
		
		strncpy(BufInc[sender], buf, len);
		BufInc[sender][len] = '\0';
	    } else {
		/* buffer is used, append the msg */
		int leninc = strlen(BufInc[sender]);
		if (db) fprintf(stderr, "sender:%i no newline ->append :%s\n", 
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
	
	if (db) fprintf(stderr, "string to global sender:%i "
			"outfd:%i : %s\n", sender, outfd, savep);
	
	/* setup new client list item */
	newMsg = (OutputBuffers *)umalloc(sizeof(OutputBuffers), __func__);
	newMsg->time = time(0); 
	if (outfd == STDERR_FILENO) {
	    newMsg->outfp = stderr;
	} else {
	    newMsg->outfp = stdout;
	}
	
	/* check if already in global buffer */
	hash = calcHash(savep);

	if ((globalMsg = isSaved(savep, hash))) {
	    /* string is already in global msg cache, just save ref to it */
	    (globalMsg->counter)++;
	    newMsg->line = globalMsg->line;
	    if(db) fprintf(stderr, "pointer to matrix count:%i hash:%i "
				   "savep=%s", globalMsg->counter, 
				   globalMsg->hash, savep);
	    free(savep);
	} else {
	    /* new message, save it to global msg cache */
	    globalMsg = (bMessages *)umalloc(sizeof(bMessages), __func__);
	    globalMsg->line = savep;
	    globalMsg->hash = hash;
	    globalMsg->counter = 1;
	    list_add_tail(&(globalMsg->list), &bufGlobal.list);
	    
	    newMsg->line = savep;
	    
	    if(db) fprintf(stderr, "string to matrix  count:%i hash:%i "
				   "savep=%s", globalMsg->counter, 
				   globalMsg->hash, savep);
	}
	
	/* save the message to the client list */
	list_add_tail(&(newMsg->list), &ClientBuf->list);
	
	/* goto next line */	    
	count -= len;
	buf = nl;
    }
    free(bufmem);
}
