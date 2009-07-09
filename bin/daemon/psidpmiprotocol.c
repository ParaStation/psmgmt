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
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>

#include "pscommon.h"
#include "kvs.h"
#include "kvscommon.h"
#include "pslog.h"

#include "psidforwarder.h"
#include "psidpmiprotocol.h"

#define SOCKET_ERROR -1
#define SOCKET int
#define PMI_RESEND 5


typedef struct {
    char *Name;
    int (*fpFunc)(char *msgBuffer);
} PMI_Msg;

typedef struct {
    char *Name;
    int (*fpFunc)(void);
} PMI_shortMsg;

/** Flag to check if the pmi_init() was called successful */
static int is_init = 0;

/** Flag to check if initialisation between us and client was ok */
static int pmi_init_client = 0;

/** Prefix of the next kvs name */
static int kvs_next;

/** If set debug output is generated */
static int debug = 0;

/** If set kvs debug output is generated */
static int debug_kvs = 0;

/** The application number of the connected pmi client */
static int appnum = 0;

/** The size of the mpi universe set from mpiexec */
static int universe_size = 0;

/** The rank of the connected pmi client */
static int rank = 0;

/** Counts update kvs msg from logger to make sure all msg were received */
static int updateMsgCount;

/** Suffix of the kvs name */
static char kvs_name_tmp[KVSNAME_MAX];

/** The socket which is connected to the pmi client */
static SOCKET pmisock;

/** The logger task ID of the current job */
static PStask_ID_t loggertid;

/** The predecessor task ID of the current job */
static PStask_ID_t predtid = -1;

/** The successor task ID of the current job */
static PStask_ID_t succtid = -1;

/** Flag to indicate if we use a dasiy chain */
int useDaisyChain = 0;

/** Flag to check if we got the local barrier_in msg */
static int gotBarrierIn = 0;

/** Flag to check if we got the daisy barrier_in msg */
static int gotDaisyBarrierIn = 0;

/**
 * @brief Send KVS message to logger.
 *
 * Send a KVS messages to the logger to manipulate the global kvs.
 *
 * @param msgbuffer The buffer which contains the kvs msg to send to
 * the logger.
 *
 * @return No return value.
 */
static void sendKvstoLogger(char *msgbuffer)
{
    if (debug_kvs) {
	PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: %s\n",
			  __func__, rank, msgbuffer);
    }
    PSLog_write(loggertid, KVS, msgbuffer, strlen(msgbuffer) +1);
}

/**
 * @brief Send KVS messages to successor
 *
 * Send a KVS messages to the successor, i.e. the process with the
 * next rank. If the current process is the on with the highest rank,
 * the message will be send to the logger in order to signal
 * successful delivery to all clients.
 *
 * @param msgbuffer The buffer which contains the kvs msg to send to
 * the sucessor.
 *
 * @return No return value.
 */
static void sendKvstoSucc(char *msgbuffer)
{
    if (debug_kvs) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: %s\n", __func__, rank,
			  msgbuffer);
    }
    PSLog_write(succtid, KVS, msgbuffer, strlen(msgbuffer) +1);
}

/**
 * @brief Handle critical error.
 *
 * To handle a critical error close the connection and kill the child.
 * If something goes wrong in the startup phase with pmi, the child
 * and therefore the whole job can hang infinite. So we have to kill it.
 *
 * @return No return value.
 */
static int critErr(void)
{
    /* close connection */
    if (pmisock > 0 ) {
	close(pmisock);
	pmisock = -1;
    }

    /* kill the child */
    DDSignalMsg_t msg;

    msg.header.type = PSP_CD_SIGNAL;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.signal = -1;
    msg.param = getuid();
    msg.pervasive = 1;
    msg.answer = 0;

    sendDaemonMsg((DDMsg_t *)&msg);
    return 1;
}

/**
 * @brief Write to pmi socket.
 *
 * Write the message @a msg to the pmi socket file-descriptor,
 * starting at by @a offset of the message. It is expected that the
 * previos parts of the message were sent in earlier calls to this
 * function.
 *
 * @param msg The message to transmit.
 *
 * @param offset Number of bytes sent in earlier calls.
 *
 * @return On success, the total number of bytes written is returned,
 * i.e. usually this is the length of @a msg. If the @ref stdinSock
 * file-descriptor blocks, this might also be smaller. In this case
 * the total number of bytes sent in this and all previous calls is
 * returned. If an error occurs, -1 or 0 is returned and errno is set
 * appropriately.
 */
static int do_send(char *msg, int offset, int len)
{
    int n, i;

    if (!len || !msg) {
	return 0;
    }

    for (n=offset, i=1; (n<len) && (i>0);) {
	i = send(pmisock, &msg[n], len-n, 0);
	if (i<=0) {
	    switch (errno) {
	    case EINTR:
		break;
	    case EAGAIN:
		return n;
		break;
	    default:
		{
		char *errstr = strerror(errno);
		PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: got error %d on pmi socket: %s\n",
			  __func__, rank, errno, errstr ? errstr : "UNKNOWN");
	    return i;
		}
	    }
	} else
	    n+=i;
    }
    return n;
}

/**
 * @brief Send PMI Client message.
 *
 * Send a message to the connected pmi client.
 *
 * @param msg Buffer with the pmi message to send.
 *
 * @return Returns 1 on error, 0 on success.
 */
static int PMI_send(char *msg)
{
    ssize_t len;
    int i, written = 0;

    len = strlen(msg);
    if (!(len && msg[len - 1] == '\n')) {
	/* assert */
	PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: missing '\\n' in pmi msg '%s'\n",
			  __func__, rank, msg);
	return critErr();
    }

    if (debug) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: %s",
			  __func__, rank, msg);
    }

    /* try to send msg, repeat it PMI_RESEND times */
    for (i =0; (written < len) && (i< PMI_RESEND); i++) {
	written = do_send(msg, written, len);
    }

    if (written < len) {
	PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: failed sending %s\n",
			  __func__, rank, msg);
	return critErr();
    }

    return 0;
}

/**
 * @brief Return mpi universe size.
 *
 * Return the size of the mpi universe.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success.
 */
static int p_Get_Universe_Size(char *msgBuffer)
{
    char reply[PMIU_MAXLINE];
    snprintf(reply, sizeof(reply), "cmd=universe_size size=%i\n",
	   universe_size);

    PMI_send(reply);

    return 0;
}

/**
 * @brief Return application number.
 *
 * Returns the application number which defines the order the
 * application was started.
 *
 * @return Returns 0 for success.
 */
static int p_Get_Appnum(void)
{
    char reply[PMIU_MAXLINE];

    snprintf(reply, sizeof(reply), "cmd=appnum appnum=%i\n", appnum);
    PMI_send(reply);

    return 0;
}

/**
 * @brief Check if we can forward the daisy barrier.
 *
 * Check if we can forward the barrier if we are using daisy chain,
 * we have to wait for the barrier from the previous rank and from
 * the local mpi process
 */
static void checkDaisyBarrier()
{
    char kvsmsg[PMIU_MAXLINE];

    if (gotBarrierIn && gotDaisyBarrierIn) {
	gotBarrierIn = 0;
	gotDaisyBarrierIn = 0;

	snprintf(kvsmsg, sizeof(kvsmsg), "cmd=daisy_barrier_in\n");
	sendKvstoSucc(kvsmsg);
    }
}

/**
 * @brief Set a new barrier.
 *
 * Sets a new mpi barrier. The pmi client has to wait until all
 * clients have entered barrier.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success.
 */
static int p_Barrier_In(char *msgBuffer)
{
    char kvsmsg[PMIU_MAXLINE];

    if (useDaisyChain) {
	if (predtid == loggertid) {
	    /* if we are the first in chain, send starting barrier msg */
	    snprintf(kvsmsg, sizeof(kvsmsg), "cmd=daisy_barrier_in\n");
	    sendKvstoSucc(kvsmsg);
	} else {
	    gotBarrierIn = 1;
	    checkDaisyBarrier();
	}
    } else {
	/* forward to logger */
	sendKvstoLogger(msgBuffer);
    }

    return 0;
}

/**
 * @brief Set a new daisy barrier.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success.
 */
static int p_Daisy_Barrier_In(char *msgBuffer)
{
    if (useDaisyChain) {
	if (predtid == loggertid) {
	    PSIDfwd_printMsgf(STDERR, "%s: rank %i: received"
			      " daisy_barrier_in\n", __func__, rank);
	} else {
	   gotDaisyBarrierIn = 1;
	   checkDaisyBarrier();
	}
    } else {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: received daisy_barrier_in"
			  " without daisy-chaining\n", __func__, rank);
    }

    return 0;
}

/**
 * @brief Finalize the PMI.
 *
 * Returns PMI_FINALIZED to notice the forwarder that the
 * child has finished execution. The forwarder will release the child
 * and then call pmi_finalize() to allow the child to exit.
 *
 * @return Returns PMI_FINALIZED.
 * */
static int p_Finalize(void)
{
    char kvsmsg[PMIU_MAXLINE];

    /* leave kvs space */
    snprintf(kvsmsg, sizeof(kvsmsg), "cmd=leave_kvs\n");
    sendKvstoLogger(kvsmsg);

    return PMI_FINALIZED;
}

/**
 * @brief Return the default kvs name.
 *
 * Returns the default(_0) kvs name.
 *
 * @return Returns 0 for success.
 */
static int p_Get_My_Kvsname(void)
{
    char reply[PMIU_MAXLINE];

    snprintf(reply, sizeof(reply), "cmd=my_kvsname kvsname=%s_0\n",
	     kvs_name_tmp);

    PMI_send(reply);

    return 0;
}

/**
 * @brief Creates a new kvs.
 *
 * Creates a new key value space.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Create_Kvs(void)
{
    char kvsmsg[PMIU_MAXLINE], kvsname[KVSNAME_MAX];

    /* create new kvs name */
    snprintf(kvsname, sizeof(kvsname), "%s_%i\n", kvs_name_tmp, kvs_next++);

    /* create local kvs */
    if (!kvs_create(kvsname)) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: create kvs %s failed\n",
			  __func__, rank, kvsname);
	PMI_send("cmd=newkvs rc=-1\n");
	return 1;
    }

    /* create kvs in logger */
    snprintf(kvsmsg, sizeof(kvsmsg), "cmd=create_kvs kvsname=%s", kvsname);
    sendKvstoLogger(kvsmsg);

    return 0;
}

/**
 * @brief Delete a kvs.
 *
 * Deletes the specific key values space.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Destroy_Kvs(char *msgBuffer)
{
    char kvsname[KVSNAME_MAX];

    /* get parameter from msg */
    getpmiv("kvsname",msgBuffer,kvsname,sizeof(kvsname));

    if (kvsname[0] == 0) {
	PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: got invalid msg\n",
			  __func__, rank);
	PMI_send("cmd=kvs_destroyed rc=-1\n");
	return 1;
    }

    /* destroy kvs */
    if (kvs_destroy(kvsname)) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: error destroying kvs %s\n",
			  __func__, rank, kvsname);
	PMI_send("cmd=kvs_destroyed rc=-1\n");
	return 1;
    }

    /* forward to logger */
    sendKvstoLogger(msgBuffer);

    return 0;
}

/**
 * @brief Put a key into kvs.
 *
 * Puts a new key into a specific key value space.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Put(char *msgBuffer)
{
    char kvsname[KVSNAME_MAX], key[KEYLEN_MAX], value[VALLEN_MAX];

    getpmiv("kvsname",msgBuffer,kvsname,sizeof(kvsname));
    getpmiv("key",msgBuffer,key,sizeof(key));
    getpmiv("value",msgBuffer,value,sizeof(value));

    /* check msg */
    if (kvsname[0] == 0 || key[0] == 0 || value[0] == 0) {
	if (debug_kvs) {
	    PSIDfwd_printMsgf(STDERR,
			      "%s: rank %i: received invalid pmi put msg\n",
			      __func__, rank);
	}
	PMI_send("cmd=put_result rc=-1 msg=error_invalid_put_msg\n");
	return 1;
    }

    /* save to local kvs */
    if (kvs_put(kvsname, key, value)) {
	if (debug_kvs) {
	    PSIDfwd_printMsgf(STDERR, "%s: rank %i:"
			      " error while put key:%s value:%s to kvs:%s \n",
			      __func__, rank, key, value, kvsname);
	}
	PMI_send("cmd=put_result rc=-1 msg=error_in_kvs\n");
	return 1;
    }

    /* forward to logger */
    sendKvstoLogger(msgBuffer);

    return 0;
}

/**
 * @brief Read a kvs value.
 *
 * Reads a value from the specific key value space.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Get(char *msgBuffer)
{
    char reply[PMIU_MAXLINE], kvsname[KVSNAME_MAX], key[KEYLEN_MAX];
    char *value;

    /* extract parameters */
    getpmiv("kvsname",msgBuffer,kvsname,sizeof(kvsname));
    getpmiv("key",msgBuffer,key,sizeof(key));

    /* check msg */
    if (kvsname[0] == 0 || key[0] == 0) {
	if (debug_kvs) {
	    PSIDfwd_printMsgf(STDERR,
			      "%s: rank %i: received invalid pmi get cmd\n",
			      __func__, rank);
	}
	PMI_send("cmd=get_result rc=-1 msg=error_invalid_get_msg\n");
	return 1;
    }

    /* get value from kvs */
    if (!(value = kvs_get(kvsname, key))) {
	if (debug_kvs) {
	    PSIDfwd_printMsgf(STDERR,
			      "%s: rank %i: get on non exsisting kvs key:%s\n",
			      __func__, rank, key);
	}
	snprintf(reply, sizeof(reply),
		 "cmd=get_result rc=%i msg=error_value_not_found\n",
		 PMI_ERROR);
	PMI_send(reply);
	return 1;
    }

    /* send pmi to client */
    snprintf(reply, sizeof(reply), "cmd=get_result rc=%i value=%s\n",
	     PMI_SUCCESS, value);
    PMI_send(reply);

    return 0;
}

/**
 * @brief Publish a service.
 *
 * Make a service public even for processes which are not
 * connected over pmi.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Publish_Name(char *msgBuffer)
{
    char service[VALLEN_MAX], port[VALLEN_MAX];
    char reply[PMIU_MAXLINE];

    getpmiv("service",msgBuffer,service,sizeof(service));
    getpmiv("port",msgBuffer,port,sizeof(port));

    /* check msg */
    if (port[0] == 0 || service[0] == 0) {
	PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: received invalid publish_name msg\n",
			  __func__, rank);
	return 1;
    }

    if (debug) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: received publish name request"
			  " for service:%s, port:%s\n",
			  __func__, rank, service, port);
    }

    snprintf(reply, sizeof(reply), "cmd=publish_result info=%s\n",
	     "not_implemented_yet\n" );
    PMI_send(reply);

    return 0;
}

/**
 * @brief Unpublish a service.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Unpublish_Name(char *msgBuffer)
{
    char service[VALLEN_MAX];
    char reply[PMIU_MAXLINE];

    getpmiv("service",msgBuffer,service,sizeof(service));

    /* check msg*/
    if (service[0] == 0) {
	PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: received invalid unpublish_name msg\n",
			  __func__, rank);
    }

    if (debug) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: received unpublish name"
			  " request for service:%s\n",
			  __func__, rank, service);
    }
    snprintf(reply, sizeof(reply), "cmd=unpublish_result info=%s\n",
	     "not_implemented_yet\n" );
    PMI_send(reply);

    return 0;
}

/**
 * @brief Lookup a service name.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Lookup_Name(char *msgBuffer)
{
    char service[VALLEN_MAX];
    char reply[PMIU_MAXLINE];

    getpmiv("service",msgBuffer,service,sizeof(service));

    /* check msg*/
    if (service[0] == 0) {
	PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: received invalid lookup_name msg\n",
			  __func__, rank);
    }

    if (debug) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: received lookup name request"
			  " for service:%s\n",
			  __func__, rank, service);
    }
    snprintf(reply, sizeof(reply), "cmd=lookup_result info=%s\n",
	     "not_implemented_yet\n" );
    PMI_send(reply);

    return 0;
}

/**
 * @brief Spawn multiple processes.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Spawn(char *msgBuffer)
{
    /*
     *	@param nprocs
     *	    number of processes
     *	@param execname
     *	    name of the executalbe
     *	@param totspwans
     *	    number how many procs total spawned
     *	@param spwanssofar
     *	    number how many procs where spawned so far
     *	@param argcnt
     *	    number of arguments
     *	@param preput_num
     *	    number of preput values and keys
     *	@param info_num
     *	    number of info values and keys
    char *nprocs, *execname, *totspwans, *spwanssofar;
    char *argcnt, *preput_num, *info_num;

    nprocs = getpmiv("nprocs",msgBuffer);
    execname = getpmiv("execname",msgBuffer);
    totspwans = getpmiv("totspawns",msgBuffer);
    spwanssofar = getpmiv("spwanssofar",msgBuffer);

    argcnt = getpmiv("argcnt",msgBuffer);
    preput_num = getpmiv("preput_num",msgBuffer);
    info_num = getpmiv("info_num",msgBuffer);


    if (!nprocs) {
	printf("received wrong spwan msg\n");
	return 1;
    }


    if (argcnt) {
	int numarg, i;
	char *nextarg, argname[200];

	numarg = atoi(argcnt);
	for (i=1; i< numarg; i++) {
	    snprintf(argname,sizeof(argname),"arg%i",i);
	    nextarg = getpmiv(argname,msgBuffer);
	    if (nextarg) {
		//save arg
	    } else {
		printf("Received wrong arguments in spawn msg\n");
	    }
	}
    }

    if (preput_num) {
	int numpre, i;
	char *nextpreval, *nextprekey, prename[200];

	numpre = atoi(preput_num);
	for (i=1; i< numpre; i++) {
	    snprintf(prename,sizeof(prename),"preput_key_%i",i);
	    nextprekey = getpmiv(prename,msgBuffer);
	    snprintf(prename,sizeof(prename),"preput_val_%i",i);
	    nextpreval = getpmiv(prename,msgBuffer);
	    if (nextpreval && nextprekey ) {
		//save pre
	    } else {
		printf("Received wrong preput values in spawn msg\n");
	    }
	}
    }


    if (info_num) {
	int numinfo, i;
	char *nextinfoval, *nextinfokey, infoname[200];

	numinfo = atoi(info_num);
	for (i=1; i< numinfo; i++) {
	    snprintf(infoname,sizeof(infoname),"info_key_%i",i);
	    nextinfokey = getpmiv(infoname,msgBuffer);
	    snprintf(infoname,sizeof(infoname),"info_val_%i",i);
	    nextinfoval = getpmiv(infoname,msgBuffer);
	    if (nextinfoval && nextinfokey ) {
		//save info
	    }	else {
		printf("Received wrong infos in spawn msg\n");
	    }

	}
    }
    */

    if (debug) {
	PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: received pmi spawn msg request:%s\n",
			  __func__, rank, msgBuffer);
    }

    PMI_send("cmd=spwan_result rc=-1\n");

    PSIDfwd_printMsgf(STDERR, "%s: rank %i: received unsupported pmi spwan"
		      " request:%s\n", __func__, rank, msgBuffer);
    critErr();

    return 0;
}

/**
 * @brief Get key-value pair by index.
 *
 * Get a key-value pair by specific index from key value space.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_GetByIdx(char *msgBuffer)
{
    char reply[PMIU_MAXLINE];
    char idx[VALLEN_MAX], kvsname[KVSNAME_MAX];
    char *value, *ret, name[KEYLEN_MAX];
    int index, len;

    getpmiv("idx", msgBuffer, idx, sizeof(msgBuffer));
    getpmiv("kvsname", msgBuffer, kvsname, sizeof(msgBuffer));

    /* check msg */
    if (idx[0] == 0 || kvsname[0] == 0) {
	if (debug_kvs) {
	    PSIDfwd_printMsgf(STDERR, "%s: rank %i:"
			      " received invalid pmi getbiyidx msg\n",
			      __func__, rank);
	}
	snprintf(reply, sizeof(reply),
		 "getbyidx_results rc=-1 reason=invalid_getbyidx_msg\n");
	PMI_send(reply);
	return 1;
    }

    index = atoi(idx);
    /* find and return the value */
    if ((ret = kvs_getbyidx(kvsname,index))) {
	if (!(value = strchr(ret,'=') + 1)) {
	    PSIDfwd_printMsgf(STDERR, "%s: rank %i:"
			      " error in local key value space\n",
			      __func__, rank);
	    return critErr();
	}
	len = strlen(ret) - strlen(value) - 1;
	strncpy(name, ret, len);
	name[len] = '\0';
	snprintf(reply, sizeof(reply),
		 "getbyidx_results rc=0 nextidx=%d key=%s val=%s\n",
		 ++index, name, value);
    } else {
	snprintf(reply, sizeof(reply),
		 "getbyidx_results rc=-2 reason=no_more_keyvals\n");
    }

    PMI_send(reply);

    return 0;
}

/**
 * @brief Init PMI Communication.
 *
 * Init the PMI Communication, mainly to be sure both sides
 * speaks the same protocol.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Init(char *msgBuffer)
{
    char reply[PMIU_MAXLINE], pmiversion[20], pmisubversion[20];

    getpmiv("pmi_version",msgBuffer,pmiversion,sizeof(pmiversion));
    getpmiv("pmi_subversion",msgBuffer,pmisubversion,sizeof(pmisubversion));

    /* check msg */
    if (pmiversion[0] == 0 || pmisubversion[0] == 0) {
	PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: received invalid pmi init cmd\n",
			  __func__, rank);
	return critErr();
    }

    if (atoi(pmiversion) == PMI_VERSION
	&& atoi(pmisubversion) == PMI_SUBVERSION) {
	snprintf(reply, sizeof(reply), "cmd=response_to_init"
		 " pmi_version=%i pmi_subversion=%i rc=%i\n",
		 PMI_VERSION, PMI_SUBVERSION, PMI_SUCCESS);
	PMI_send(reply);
    } else {
	snprintf(reply, sizeof(reply), "cmd=response_to_init"
		 " pmi_version=%i pmi_subversion=%i rc=%i\n",
		 PMI_VERSION, PMI_SUBVERSION, PMI_ERROR);
	PMI_send(reply);
	PSIDfwd_printMsgf(STDERR, "%s: rank %i:"
			  " unsupported pmi version received:"
			  " version=%i, subversion=%i\n",
			  __func__, rank, atoi(pmiversion),
			  atoi(pmisubversion));
	return critErr();
    }
    pmi_init_client = 1;

    return 0;
}

/**
 * @brief Get kvs maxes.
 *
 * Get the max size of the kvsname, keylen and values.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Get_Maxes(void)
{
    char reply[PMIU_MAXLINE];
    snprintf(reply, sizeof(reply),
	     "cmd=maxes kvsname_max=%i keylen_max=%i vallen_max=%i\n",
	     KVSNAME_MAX, KEYLEN_MAX, VALLEN_MAX);
    PMI_send(reply);

    return 0;
}

/**
 * @brief Intel MPI 3.0 Extension.
 *
 * PMI extension in Intel MPI since version 3.0, just to recognize it.
 *
 * response msg put_ranks2hosts:
 *
 * 1. msg-> put_ranks2hosts <msglen> <num_of_hosts>
 *  <msglen>: the number of characters in the next msg + 1
 *  <num_of_hosts>: total number of the non-recurring
 *	host names that are in the set
 *
 * 2. msg-> <hnlen> <hostname> <rank1,rank2,...,rankN,>
 *  <hnlen> number of characters in the next <hostname> field
 *  <hostname> the node name
 *  <rank1,rank2,..,rankN> a comma seperated list of ranks executed on the
 *	<hostname> node; if the list is the last in the response, it must be
 *	followed a blank space
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Get_Rank2Hosts(void)
{
    char reply[PMIU_MAXLINE];
    if (debug) {
	PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: got get_rank2hosts request\n",
			  __func__, rank);
    }

    snprintf(reply, sizeof(reply),
	     "cmd=put_ranks2hosts 0 0\n");
    PMI_send(reply);

    return 0;
}

/**
 * @brief Authenticate client.
 *
 * Use handshake to authenticate the client.
 *
 * @param msgBuffer The buffer which contains the pmi
 * msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_InitAck(char *msgBuffer)
{
    char *pmi_id, client_id[KEYLEN_MAX], reply[PMIU_MAXLINE];

    if (debug) {
	PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: received pmi initack msg:%s\n",
			  __func__, rank, msgBuffer);
    }

    if (!(pmi_id = getenv("PMI_ID"))) {
	PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: no PMI_ID is set\n",
			  __func__, rank);
	return critErr();
    }

    getpmiv("pmiid", msgBuffer, client_id, sizeof(client_id));

    if (client_id[0] == 0) {
	PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: invalid initack from client\n",
			  __func__, rank);
	return critErr();
    }

    if (!(strcmp(pmi_id, client_id))) {
	PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: invalid pmi_id from client\n",
			  __func__, rank);
	return critErr();
    }

    PMI_send("cmd=initack rc=0\n");
    snprintf(reply, sizeof(reply), "cmd=set size=%i\n", universe_size);
    PMI_send(reply);
    snprintf(reply, sizeof(reply), "cmd=set rank=%i\n", rank);
    PMI_send(reply);
    snprintf(reply, sizeof(reply), "cmd=set debug=%i\n", debug);
    PMI_send(reply);

    return 0;
}

/**
 * @brief Handle execution problem message.
 *
 * The execution problem message  is sent BEFORE client actually
 * starts, so even before PMIinit.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Execution_Problem(char *msgBuffer)
{
    char exec[VALLEN_MAX], reason[VALLEN_MAX];

    getpmiv("reason",msgBuffer,exec,sizeof(exec));
    getpmiv("exec",msgBuffer,reason,sizeof(reason));

    if (exec[0] == 0 || reason[0] == 0) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i:"
			  " received invalid pmi execution problem msg\n",
			  __func__, rank);
	return 1;
    }

    PSIDfwd_printMsgf(STDERR,
		      "%s: rank %i: execution problem: exec=%s, reason=%s\n",
		      __func__, rank, exec, reason);

    return 0;
}

const PMI_Msg pmi_commands[] =
{
	{ "destroy_kvs",		&p_Destroy_Kvs		},
	{ "put",			&p_Put			},
	{ "get",			&p_Get			},
	{ "publish_name",		&p_Publish_Name		},
	{ "unpublish_name",		&p_Unpublish_Name	},
	{ "lookup_name",		&p_Lookup_Name		},
	{ "spwan",			&p_Spawn		},
	{ "barrier_in",			&p_Barrier_In		},
	{ "init",			&p_Init			},
	{ "execution_problem",		&p_Execution_Problem	},
	{ "getbyidx",			&p_GetByIdx		},
	{ "get_universe_size",		&p_Get_Universe_Size	},
	{ "initack",			&p_InitAck		},
};

const PMI_shortMsg pmi_short_commands[] =
{
	{ "get_appnum",			&p_Get_Appnum		},
	{ "finalize",			&p_Finalize		},
	{ "get_my_kvsname",		&p_Get_My_Kvsname	},
	{ "create_kvs",			&p_Create_Kvs		},
	{ "get_maxes",			&p_Get_Maxes		},
	{ "get_ranks2hosts",		&p_Get_Rank2Hosts	},
};


const int pmi_com_count = sizeof(pmi_commands)/sizeof(pmi_commands[0]);
const int pmi_short_com_count =
    sizeof(pmi_short_commands)/sizeof(pmi_short_commands[0]);

/**
 * @brief Init the PMI interface.
 *
 * Init the PMI interface, this must be the first call before
 * everything else.
 *
 * @param pmisocket The socket witch is connect to the pmi client.
 *
 * @param loggertaskid The task id of the logger.
 *
 * @param rank The rank of the pmi client.
 *
 * @return Returns 0 on success and 1 on errors.
 */
int pmi_init(int pmisocket, PStask_ID_t loggertaskid, int pRank)
{
    char *env_debug, *env_kvs_name;
    char kvsmsg[PMIU_MAXLINE], kvsname[KVSNAME_MAX];

    rank = pRank;
    loggertid = loggertaskid;
    pmisock = pmisocket;

    if (pmisocket < 1) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: invalid pmi socket\n",
			  __func__, rank);
	return 1;
    }

    /* set debug mode */
    if ((env_debug = getenv("PMI_DEBUG")) && atoi(env_debug) > 0) {
	debug = atoi(env_debug);
	debug_kvs = debug;
    } else if ((env_debug = getenv("PMI_DEBUG_CLIENT"))) {
	debug = atoi(env_debug);
    }
    if ((env_debug = getenv("PMI_DEBUG_KVS"))) {
	debug_kvs = atoi(env_debug);
    }

    /* set the mpi universe size */
    if ((env_debug = getenv("PMI_UNIVERSE_SIZE"))) {
	universe_size = atoi(env_debug);
    } else {
	universe_size = 1;
    }

    /* set the name of the kvs space */
    if (!(env_kvs_name = getenv("PMI_KVS_TMP"))) {
	strncpy(kvs_name_tmp,"kvs_localhost",sizeof(kvs_name_tmp) -1);
    } else {
	snprintf(kvs_name_tmp, sizeof(kvs_name_tmp), "kvs_%s", env_kvs_name);
    }

    appnum = 0;
    kvs_next = 0;
    is_init = 1;
    updateMsgCount = 0;

    /* default kvs name */
    snprintf(kvsname, sizeof(kvsname), "%s_%i", kvs_name_tmp, kvs_next++);

    /* create local kvs space */
    kvs_init();
    if (kvs_create(kvsname)) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: error creating local kvs\n",
			  __func__, rank);
	return critErr();
    }

    /* join kvs space */
    snprintf(kvsmsg, sizeof(kvsmsg), "cmd=join_kvs\n");
    sendKvstoLogger(kvsmsg);

    return 0;
}

void pmi_set_pred(PStask_ID_t pred)
{
    predtid = pred;
}

void pmi_set_succ(PStask_ID_t succ)
{
    succtid = succ;
}

/**
 * @brief Extract PMI command.
 *
 * Parse a pmi message and return the command.
 *
 * @param msg The message to parse.
 *
 * @param cmdbuf The buffer which receives the extracted command.
 *
 * @param bufsize The size of cmdbuf.
 *
 * @return Returns 1 for success, 0 on errors.
 */
static int pmi_extract_cmd(char *msg, char *cmdbuf, int bufsize)
{
    const char delimiters[] =" \n";
    char *msgCopy, *cmd, *saveptr;

    if (!msg || strlen(msg) < 5) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: invalid pmi msg received\n",
			  __func__, rank);
	return !critErr();
    }

    msgCopy = strdup(msg);
    cmd = strtok_r(msgCopy, delimiters, &saveptr);

    while ( cmd != NULL ) {
	if (!strncmp(cmd,"cmd=", 4)) {
	    cmd += 4;
	    strncpy(cmdbuf, cmd, bufsize);
	    free(msgCopy);
	    return 1;
	}
	if (!strncmp(cmd,"mcmd=", 5)) {
	    cmd += 5;
	    strncpy(cmdbuf, cmd, bufsize);
	    free(msgCopy);
	    return 1;
	}
	cmd = strtok_r(NULL, delimiters, &saveptr);
    }

    free(msgCopy);

    return 0;
}

/**
 * @brief PMI message switch.
 *
 * Parse a pmi msg and call the appropriate protocol handler
 * function.
 *
 * @param msg The pmi message to parse.
 *
 * @return Returns 0 for success, 1 on errors.
 */
int pmi_parse_msg(char *msg)
{
    int i;
    char cmd[VALLEN_MAX];
    char reply[PMIU_MAXLINE];

    if (is_init != 1) {
	PSIDfwd_printMsgf(STDERR,
			  "%s: rank %i: you must call pmi_init first\n",
			  __func__, rank);
	return 1;
    }

    if (strlen(msg) > PMIU_MAXLINE ) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: pmi msg to long,"
			  " msg_size=%i and allowed_size=%i\n",
			  __func__, rank, (int)strlen(msg), PMIU_MAXLINE);
	return critErr();
    }

    if (!pmi_extract_cmd(msg, cmd, sizeof(cmd)) || strlen(cmd) <2) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: invalid pmi cmd received,"
			  " msg was:%s\n", __func__, rank, msg);
	return critErr();
    }

    if (debug) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: received pmi msg:%s\n",
			  __func__, rank, msg);
    }

    /* find pmi cmd */
    for (i=0; i< pmi_com_count; i++) {
	if (!strcmp(cmd,pmi_commands[i].Name)) {
		return pmi_commands[i].fpFunc(msg);
	}
    }

    /* find short pmi cmd */
    for (i=0; i< pmi_short_com_count; i++) {
	if (!strcmp(cmd,pmi_short_commands[i].Name)) {
		return pmi_short_commands[i].fpFunc();
	}
    }

    /* cmd not found */
    PSIDfwd_printMsgf(STDERR, "%s: rank %i: unsupported pmi cmd received:%s\n",
		      __func__, rank, cmd);

    snprintf(reply, sizeof(reply),
	     "cmd=%s rc=%i info=not_supported_cmd\n", cmd, PMI_ERROR);
    PMI_send(reply);

    return critErr();
}

/**
* @brief Release the PMI client.
*
* Finalize the pmi connection and release the pmi client.
*
* @return No return value.
*/
void pmi_finalize(void)
{
    if (pmi_init_client) {
	PMI_send("cmd=finalize_ack\n");
    }
}

/**
* @brief Handle kvs reply from logger.
*
* Handle a key value space message reply send from the logger.
*
* @return No return value.
*/
void pmi_handleKvsRet(PSLog_Msg_t *msg)
{
    char cmd[VALLEN_MAX], reply[PMIU_MAXLINE];
    char *nextvalue, *saveptr, *value, vname[KEYLEN_MAX], kvsname[KEYLEN_MAX];
    const char delimiters[] =" \n";
    int len;

    if (debug_kvs) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: %s\n",
			  __func__, rank, msg->buf);
    }

    /* extract cmd from msg */
    if (!pmi_extract_cmd(msg->buf, cmd, sizeof(cmd)) || strlen(cmd) < 2) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: received invalid kvs msg"
			  " from logger\n", __func__, rank);
	critErr();
	return;
    }

    /* kvs is disabled */
    if (!strcmp(cmd, "kvs_not_available")) {
	PSIDfwd_printMsgf(STDERR, "%s: rank %i: global kvs is not available,"
			  " exiting\n", __func__, rank);
	critErr();
	return;
    }

    /* update kvs after barrier_in */
    if (!strcmp(cmd, "kvs_update_cache")) {
	char *msgCopy = NULL;
	if (msg->header.sender == predtid && succtid != -1) {
	    msgCopy = strdup(msg->buf);
	}

	nextvalue = strtok_r(msg->buf, delimiters, &saveptr);
	nextvalue = strtok_r(NULL, delimiters, &saveptr);
	while (nextvalue != NULL) {
	    /* extract next key/value pair */
	    if (!(value = strchr(nextvalue, '=') +1)) {
		PSIDfwd_printMsgf(STDERR, "%s: rank %i: invalid kvs update"
				  " received\n", __func__, rank);
		critErr();
		return;
	    }
	    len = strlen(nextvalue) - strlen(value) - 1;
	    strncpy(vname, nextvalue, len);
	    vname[len] = '\0';
	    /* kvsname */
	    if (!strcmp(vname,"kvsname")) {
		strncpy(kvsname, value, sizeof(kvsname));
	    } else if (strlen(kvsname) > 1) {
		/* save key/value to kvs */
		if (kvs_put(kvsname, vname, value)) {
		    PSIDfwd_printMsgf(STDERR,
				      "%s: rank %i: error saving kvs update:"
				      " kvsname:%s, key:%s, value:%s\n",
				      __func__, rank, kvsname, vname, value);
		    return;
		}
	    } else {
		PSIDfwd_printMsgf(STDERR, "%s: rank %i: received invalid"
				  " update kvs request from logger\n",
				  __func__, rank);
		return;
	    }
	    nextvalue = strtok_r( NULL, delimiters, &saveptr);
	}
	updateMsgCount++;
	if (msg->header.sender == predtid && succtid != -1) {
	    useDaisyChain = 1;
	    if (succtid != loggertid && msgCopy) sendKvstoSucc(msgCopy);
	}
	if (msgCopy) free(msgCopy);
    } else if (!strcmp(cmd, "kvs_update_cache_finish")) {
	/* forward to next client */
	if (msg->header.sender == predtid && succtid != -1) {
	    useDaisyChain = 1;
	    if (succtid != loggertid) {
		/* forward to next client */
		sendKvstoSucc(msg->buf);
	    } else {
		/* we are last in daisy chain, send logger update result */
		snprintf(reply, sizeof(reply),
			 "cmd=kvs_update_cache_result mc=%i\n", updateMsgCount);
		sendKvstoLogger(reply);
	    }
	} else if (msg->header.sender == loggertid
		   && (predtid != loggertid || succtid == -1)) {
	    /* no daisy chain, everbody has to acknowledge the logger  */
	    useDaisyChain = 0;
	    sendKvstoLogger(reply);
	}

	updateMsgCount = 0;
    } else if (!strcmp(cmd, "barrier_out")) {
	if (msg->header.sender == predtid && succtid != -1) {
	    useDaisyChain = 1;
	    if (succtid != loggertid) {
		/* forward to next client */
		sendKvstoSucc(msg->buf);
	    }
	} else if (msg->header.sender == loggertid
		   && (predtid != loggertid || succtid == -1)) {
	    /* no daisy chain */
	    useDaisyChain = 0;
	}
	/* Forward msg from logger to client */
	PMI_send(msg->buf);
    } else if (!strcmp(cmd, "daisy_barrier_in")) {
	p_Daisy_Barrier_In(cmd);
    } else {
	/* Forward msg from logger to client */
	PMI_send(msg->buf);
    }
}
