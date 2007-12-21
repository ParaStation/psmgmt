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
    int outfd;
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
bMessages BufMsgs;

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
 * @brief Init the merge structures/functions of the logger. 
 * This function must be called bevor any other merge function.
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

    ClientOutBuf = malloc (sizeof(*ClientOutBuf) * maxClients);
    BufInc = malloc (sizeof(char*) * maxClients);

    if (!ClientOutBuf || !BufInc) {
	fprintf(stderr, "PSIlogger: %s: malloc) failed.\n", __func__);
	exit(1);
    }
    
    snprintf(npsize, sizeof(npsize), "[0-%i]", np -1);
    prelen = strlen(npsize);
  
    /* Init Output Buffer List */
    for (i=0; i<maxClients; i++) { 
	ClientOutBuf[i].line = NULL;
	INIT_LIST_HEAD(&ClientOutBuf[i].list);
	BufInc[i] = NULL;
    }

    INIT_LIST_HEAD(&BufMsgs.list);
}

/**
 * @brief If more clients are connected than maxClients,
 * the caches have to be reallocated so all new clients can be
 * handled.
 *
 * @param msg The msg from the client which is greater than
 * maxClients.
 *
 * @return No return value.
 */
void reallocClientOutBuf(PSLog_Msg_t *msg)
{
    int i;

    ClientOutBuf = realloc(ClientOutBuf, sizeof(*ClientOutBuf) * 2 * msg->sender);
    BufInc = realloc(BufInc, sizeof(char*) * 2 * msg->sender);

    if (!ClientOutBuf || !BufInc) {
	fprintf(stderr, "PSIlogger: %s: realloc(%ld) failed.\n", __func__,
		(long) sizeof(*ClientOutBuf) * 2 * msg->sender);
	exit(1);
    }	    

    for (i=0; i<2*msg->sender; i++) {
	INIT_LIST_HEAD(&ClientOutBuf[i].list);
    }
    
    for (i=maxClients; i<2*msg->sender; i++) {
	ClientOutBuf[i].line = NULL;
	BufInc[i] = NULL;
    }

    maxClients = 2*msg->sender;
}

/**
 * @brief Searches the client structure and builds up a new array 
 * with of equal lines.
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
static void findEqualData(int ClientIdx, struct list_head *saveBuf[maxClients], int saveBufInd[maxClients], int *mcount, OutputBuffers *val)
{
    int dcount = 0;
    int x;
    struct list_head *npos;
    OutputBuffers *nval;

    *mcount = 0;
    for(x=0; x< maxClients; x++) {
	if (x == ClientIdx) continue;

	if (!list_empty(&ClientOutBuf[x].list)) {
	    /*found next rank */
	    list_for_each(npos, &ClientOutBuf[x].list) {
		dcount++;
		/* get data to compare */
		nval = list_entry(npos, OutputBuffers, list);	 
		
		if (!val || !nval || !val->line || !nval->line) {
		    //fprintf(stderr, "Breaking search for %s, possible error?\n", val->line);
		    break;
		}
		if (val->line == nval->line ) {
		    if (val->outfd == nval->outfd) {
			saveBuf[*mcount] = npos;
			saveBufInd[*mcount] = x; 
			(*mcount)++;
			break;
		    } else {
			//fprintf(stderr, "same line diff outfd:val->line, val: %i nval: %i\n", val->outfd, nval->outfd);
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
 * global buffer is counter is 0.
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
    if (!list_empty(&BufMsgs.list)) {
	list_for_each(pos, &BufMsgs.list) {
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
	fprintf(stderr, "%s: list empty: possible error in ouput buffer\n", __func__);
	return;
    }
    if (!found) {
	    fprintf(stderr, "%s: line not found: possible error in ouput buffer\n", __func__);
	    return;
    }
    if (!val) {
	fprintf(stderr, "%s: empty result: possible error in ouput buffer\n", __func__);
	return;
    }
    (val->counter)--;
    if (val->counter == 0) {
	free(val->line);
	free(val);
	list_del(pos);
    }
    free(nval);
    list_del(npos);
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
static void generatePrefix(char *prefix, int size, int mcount, int start, int saveBufInd[maxClients])
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
 * @param outfd The filedescriptor to write to.
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
static void printLine(int outfd, char *line, int mcount, int start, int saveBufInd[maxClients])
{
    char format[50];
    char prefix[100]; 
    int space = 0;
    FILE *out;

    if (!(out = fdopen(outfd, "a"))) {
	fprintf(stderr, "%s: Could not open file deskriptor\n", __func__);
	exit(1);
    }
    generatePrefix(prefix, sizeof(prefix), mcount, start, saveBufInd);
    space = prelen - strlen(prefix) - 2;
    if (space >0) {
	snprintf(format, sizeof(format), "%%%is[%%s]: %%s", space);  
	fprintf(out, format, " ", prefix, line);
    } else {
	snprintf(format, sizeof(format), "[%%s]: %%s");  
	fprintf(out, format, prefix, line);
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
static void outputSingleCMsg(int client, struct list_head *pos, struct list_head *saveBuf[maxClients], int saveBufInd[maxClients], int mcount)
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
			if (oval->line == nval->line && oval->outfd == nval->outfd) {
			    savelocInd[lcount] = i;
			    lcount++;
			    delCachedMsg(oval, tmpother);
			}
		    } else {
			break;
		    }
		}
	    }
	    printLine(nval->outfd, nval->line, lcount, client, savelocInd);	    
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
    FILE *out;
    
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

		//fprintf(stderr, "Analyse=np:%i mcount:%i timediff:%i flush:%i val:%s\n", np, mcount, (int )(ltime - val->time), flush, val->line);	
		if (mcount == np -1 || ltime - val->time > maxMergeWait || flush ) {
		    
		    /* output single msg from tracked rank */
		    outputSingleCMsg(i, pos, saveBuf, saveBufInd, mcount);
	     
		    for (z=0; z < mcount; z++) {
			/* ouput single msg from other ranks */
			outputSingleCMsg(saveBufInd[z], saveBuf[z], saveBuf, saveBufInd, mcount);
			
			/* remove already displayed msg from all other ranks */
			nval = list_entry(saveBuf[z], OutputBuffers, list);
			delCachedMsg(nval,saveBuf[z]);
		    }
		    
		    if (mcount != np -1) {
			printLine(val->outfd, val->line, mcount, i, saveBufInd);	    
		    } else {
			snprintf(prefix, sizeof(prefix), "[0-%i]", np -1);
			out = fdopen(val->outfd, "a");
			fprintf(out, "%s: %s", prefix, val->line);
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
static bMessages *isSaved(char *msg)
{
    struct list_head *pos;
    bMessages *val;
    int msgHash;
    
    msgHash = calcHash(msg);
    
    if (!list_empty(&BufMsgs.list)) {
	list_for_each(pos, &BufMsgs.list) {
	    /* get element to compare */
	    val = list_entry(pos, bMessages, list); 
	    
	    if(!val || !val->line) {
		break;
	    }
	    if (val->hash == msgHash && (!strcmp(val->line, msg))) {
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
    OutputBuffers *tmp = NULL;
    OutputBuffers *ClientBuf = &ClientOutBuf[sender];
    bMessages *tmpbMsg = NULL;
    char *buf, *bufmem;
    int len;
    int db = 0;
    
    bufmem = malloc(count +1);
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
		if (db) fprintf(stderr, "from %i: no newline ->newbuf :%s\n", sender, buf);
		if (!(BufInc[sender] = malloc(len +1))) {
		}
		strncpy(BufInc[sender], buf, len);
		BufInc[sender][len] = '\0';
	    } else {
		/* buffer is used, append the msg */
		int leninc = strlen(BufInc[sender]);
		if (db) fprintf(stderr, "from %i: no newline ->append :%s\n", sender, buf);
		if (!(BufInc[sender] = realloc(BufInc[sender], leninc + len +1))) {
		}
		strncat(BufInc[sender], buf, len);
		BufInc[sender][len + leninc] = '\0';
	    }
	    break;
	}
	
	/* complete msg with nl */
	len = strlen(buf) - strlen(nl);
	if (BufInc[sender]) {
	    int leninc = strlen(BufInc[sender]);
	    if (!(BufInc[sender] = realloc(BufInc[sender], leninc + len +1))) {
		fprintf(stderr, "PSIlogger: %s: out of memory\n", __func__);
		exit(1);
	    }
	    strncat(BufInc[sender], buf, len);
	    savep = BufInc[sender];
	    savep[leninc + len] = '\0';
	    BufInc[sender] = NULL;
	} else {
	    if (!(savep = malloc(len +1))) {
		fprintf(stderr, "PSIlogger: %s: out of memory\n", __func__);
		exit(1);
	    }
	    strncpy(savep, buf, len);
	    savep[len] = '\0';
	}
	
	if (db) fprintf(stderr, "string to save to global from:%i outfd:%i : %s\n", sender, outfd, savep);
	
	/* save to client matrix */
	tmp = (OutputBuffers *)malloc(sizeof(OutputBuffers));
	tmp->time = time(0); 
	tmp->outfd = outfd;
	
	/* check if already in global buffer */
	if ((tmpbMsg = isSaved(savep))) {
	    (tmpbMsg->counter)++;
	    /* save pointer from client buffer to global buffer */
	    tmp->line = tmpbMsg->line;
	    if(db) fprintf(stderr, "adding pointer to matrix: savep=%s\n",savep);
	    free(savep);
	} else {
	    if(db) fprintf(stderr, "saving to matrix: savep=%s\n",savep);

	    /* save to global buffer */	
	    tmpbMsg = (bMessages *)malloc(sizeof(bMessages));
	    tmpbMsg->line = savep;
	    tmpbMsg->hash = calcHash(savep);
	    tmpbMsg->counter = 1;
	    list_add_tail(&(tmpbMsg->list), &BufMsgs.list);
	    
	    /* save pointer from client buffer to global buffer */
	    tmp->line = savep;
	}
	if (db) fprintf(stderr,"line:%s hash:%i, count:%i\n",tmpbMsg->line, tmpbMsg->hash, tmpbMsg->counter);
	list_add_tail(&(tmp->list), &ClientBuf->list);
	
	/* next line */	    
	count -= len;
	buf = nl;
    }
    free(bufmem);
}
