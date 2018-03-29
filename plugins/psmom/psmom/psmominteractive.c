/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <grp.h>
#include <sys/types.h>
#include <pwd.h>

#include <sys/socket.h>
#include <netdb.h>

#include "psmomlog.h"
#include "psmomconv.h"
#include "psmomlocalcomm.h"
#include "psserial.h"
#include "pluginmalloc.h"
#include "pluginpty.h"
#include "selector.h"
#include "psmomtcp.h"
#include "pbsdef.h"
#include "psmomenv.h"
#include "psmomforwarder.h"

#include "psmominteractive.h"

#define _PATH_TTY "/dev/tty"
#define X11_AUTH_CMD "/usr/bin/xauth"
#define X11_PORT_RANGE_START	10
#define X11_PORT_RANGE_END	500

/** the communication handle which is connected to the remote qsub process */
static ComHandle_t *qsub_com = NULL;

/** the address where the qsub process is waiting for new connections */
static char *qsub_addr = NULL;

/** file descriptor which is connected to the local terminal */
static int term_stdin = -1;

/**
 * @brief Terminate the qsub session.
 *
 * Close all connections and kill the interactive terminal.
 *
 * @return No return value.
 */
static void terminateQsubSession(void)
{
    /* close connection to qsub */
    closeQsubConnection();

    /* kill the terminal */
    killForwarderChild(NULL);
}

/**
 * @brief Forward data between qsub and the local X11 application.
 *
 * @param fd The file descriptor where new data arrived.
 *
 * @param info Not used, required for the selector facility.
 *
 * @return Always returns 0.
 */
static int forwardX11ClientData(int fd, void *info)
{
    char buf[FORWARD_BUFFER_SIZE];
    ComHandle_t *com, *x11Com, *qCom;
    Job_Conn_t *con;
    ssize_t len, written, towrite;

    if (!(com = findComHandle(fd, TCP_PROTOCOL))) {
	mlog("%s: com handle for '%i' not found\n", __func__, fd);
	Selector_remove(fd);
	close(fd);
	return 0;
    }

    if (!(con = getJobConnByCom(com, JOB_CON_X11_CLIENT))) {
	mlog("%s: finding connection for '%i' failed\n", __func__, fd);
	Selector_remove(fd);
	close(fd);
	return 0;
    }
    qCom = con->com;
    x11Com = con->comForward;

    /* read new data to forward */
    if ((len = wReadAll(com, buf, sizeof(buf))) < 1) {
	closeJobConn(con);
	return 0;
    }

    towrite = len;
    while (towrite >0) {
	if (com == x11Com) {
	    /* from psmom(xclient) to qsub(xserver) */
	    if ((written = wWrite(qCom, buf, len)) == -1) {
		mlog("%s: error while write to qsub\n", __func__);
		closeJobConn(con);
		break;
	    }
	    wDoSend(qCom);
	} else if (com == qCom) {
	    /* from qsub(xserver) to psmom(xclient) */
	    if ((written = wWrite(x11Com, buf, len)) == -1) {
		mlog("%s: error while write to x11 client\n", __func__);
		closeJobConn(con);
		break;
	    }
	    wDoSend(x11Com);
	} else {
	    mlog("%s: invalid com handles\n", __func__);
	    return 0;
	}
	towrite -= written;
    }
    return 0;
}

/**
 * @brief Forward data from interactive terminal to qsub.
 *
 * @param sock The socket connected to the local terminal.
 *
 * @param data Not used, but required by the selector facility.
 *
 * @return Retruns 0 on success and 1 on error.
 */
static int handleNewTermData(int sock, void *data)
{
    char buf[FORWARD_BUFFER_SIZE];
    ssize_t len;

    if (term_stdin == -1) {
	mlog("%s: invalid term stdin socket '%i'\n", __func__,
	    term_stdin);
	terminateQsubSession();
	return 1;
    }

    if ((len = read(term_stdin, buf, sizeof(buf))) <= 0) {
	if (errno == EINTR) {
	    handleNewTermData(term_stdin, data);
	}
	mdbg(PSMOM_LOG_VERBOSE, "%s: connection to terminal closed\n", __func__);
	terminateQsubSession();
	return 1;
    }

    wWrite(qsub_com, buf, len);
    if ((wDoSend(qsub_com)) < 0) {
	mlog("%s: error sending data to qsub\n", __func__);
	terminateQsubSession();
    }

    return 0;
}

/**
 * @brief Forward data from qsub to interactive terminal.
 *
 * @param sock The socket connected to qsub.
 *
 * @param data Not used, but required by the selector facility.
 *
 * @return Retruns 0 on success and 1 on error.
 */
static int handleNewQsubData(int sock, void *data)
{
    char buf[FORWARD_BUFFER_SIZE];
    ssize_t len;

    if (term_stdin == -1) {
	mlog("%s: invalid term stdin socket '%i'\n", __func__,
	    term_stdin);
	terminateQsubSession();
	return 1;
    }

    if ((len = wReadAll(qsub_com, buf, sizeof(buf))) <= 0) {
	mlog("%s: connection to qsub closed : %s\n", __func__,
		strerror(errno));
	terminateQsubSession();
	return 1;
    }

    if ((doWrite(term_stdin, buf, len)) != len) {
	mlog("%s: not all data could be written : %s\n",
	    __func__, strerror(errno));
	terminateQsubSession();
	return 1;
    }

    return 0;
}

int writeQsubMessage(char *msg, size_t len)
{
    /* write message to qsub stdout */
    wWrite(qsub_com, msg, len);
    return wDoSend(qsub_com);
}

void handle_Local_Qsub_Out(ComHandle_t *com)
{
    char buf[FORWARD_BUFFER_SIZE];
    size_t len, toread;

    ReadDigitL(com, (signed long *) &toread);

    if (toread > (ssize_t) sizeof(buf)) {
	mlog("%s: buffer to small!\n", __func__);
	terminateQsubSession();
	return;
    }

    if ((len = wRead(com, buf, toread)) != toread) {
	mlog("%s: msg could not be read\n", __func__);
	terminateQsubSession();
	return;
    }

    if ((writeQsubMessage(buf, len)<0)) {
	terminateQsubSession();
    }
}

ComHandle_t *initQsubConnection(Job_t *job, Inter_Data_t *data, int term)
{
    char *addr;
    int socket;

    /* save term stdin */
    term_stdin = term;

    /* get qsub host */
    if (!(addr = getEnvValue("PBS_O_HOST"))) {
	fprintf(stderr, "%s: failed to get qsub host from PBS_O_HOST: '%s'\n",
		__func__, addr);
	return NULL;
    }
    qsub_addr = ustrdup(addr);

    /* connect to waiting qsub */
    if (!(socket = tcpConnect(job->qsubPort, qsub_addr, 0))) {
	fprintf(stderr, "%s: connection to qsub failed: '%s:%i'\n", __func__,
		qsub_addr, job->qsubPort);
	return NULL;
    }

    qsub_com = getComHandle(socket, TCP_PROTOCOL);
    qsub_com->HandleFunc = &handleNewQsubData;
    if (!qsub_com->jobid) qsub_com->jobid = ustrdup(job->id);

    /* send jobid to qsub */
    wWrite(qsub_com, job->id, strlen(job->id) + 1);
    if ((wDoSend(qsub_com)) < 0) {
	fprintf(stderr, "%s: writing jobid to qsub failed\n", __func__);
	return NULL;
    }

    /* read terminal type */
    if ((wRead(qsub_com, data->termtype, QSUB_DATA_SIZE)) != QSUB_DATA_SIZE) {
	fprintf(stderr, "%s: error reading qsub termtype\n", __func__);
	return NULL;
    }

    /* read terminal options */
    if ((wRead(qsub_com, data->termcontrol, QSUB_DATA_CONTROL))
	    != QSUB_DATA_CONTROL) {
	fprintf(stderr, "%s: error reading qsub termcontrol options\n",
		    __func__);
	return NULL;
    }

    /* read window size */
    if ((wRead(qsub_com, data->windowsize, QSUB_DATA_SIZE)) != QSUB_DATA_SIZE) {
	fprintf(stderr, "%s: error reading qsub window size\n", __func__);
	return NULL;
    }

    /* wait until the interactive terminal is ready */
    wDisable(qsub_com);

    return qsub_com;
}

void enableQsubConnection()
{
    Selector_register(term_stdin, handleNewTermData, NULL);
    wEnable(qsub_com);
}

void closeQsubConnection()
{
    /* close connection to qsub */
    if (qsub_com) {
	if ((isValidComHandle(qsub_com))) {
	    wClose(qsub_com);
	}
	qsub_com = NULL;
    }

    /* close connection to terminal */
    if (term_stdin > -1) {
	if ((Selector_isRegistered(term_stdin))) {
	    Selector_remove(term_stdin);
	    close(term_stdin);
	    term_stdin = -1;
	}
    }
}

ComHandle_t *getQsubConnection(Job_t *job)
{
    ComHandle_t *com;
    int sock;

    if (!qsub_addr) {
	mlog("%s: invalid qsub addr '%s'\n", __func__, qsub_addr);
	return NULL;
    }

    /* the job is interactive so connect to waiting qsub */
    if ((sock = tcpConnect(job->qsubPort, qsub_addr, 0))< 0) {
	mlog("%s: connection to qsub failed: '%s:%i'\n", __func__,
		qsub_addr, job->qsubPort);
	return NULL;
    }

    com = getComHandle(sock, TCP_PROTOCOL);
    com->HandleFunc = forwardX11ClientData;

    return com;
}

int setSocketNonBlocking(int socket)
{
    int flags;

    /* Set socket to non-blocking */
    if ((flags = fcntl(socket, F_GETFL, 0)) < 0) {
	mlog("%s: making socket '%i' nonblocking failed (F_GETFL) : %s\n",
	    __func__, socket, strerror(errno));
    }

    if (fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
	mlog("%s: making socket '%i' nonblocking failed (F_SETFL) : %s\n",
	    __func__, socket, strerror(errno));
    }
    return 0;
}

int setTermOptions(char *termOpt, int pty)
{
    struct termios termios;

#ifdef IMAXBEL
    termios.c_iflag = (BRKINT | IGNPAR | ICRNL | IXON | IXOFF | IMAXBEL);

#else
    termios.c_iflag = (BRKINT | IGNPAR | ICRNL | IXON | IXOFF);

#endif
    termios.c_oflag = (OPOST | ONLCR);

#if defined(ECHOKE) && defined(ECHOCTL)
    termios.c_lflag = (ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOKE | ECHOCTL);

#else
    termios.c_lflag = (ISIG | ICANON | ECHO | ECHOE | ECHOK);

#endif
    termios.c_cc[VEOL]   = '\0';

    termios.c_cc[VEOL2]  = '\0';

    termios.c_cc[VSTART] = '\021';  /* ^Q */

    termios.c_cc[VSTOP]  = '\023';  /* ^S */

#if defined(VDSUSP)
    termios.c_cc[VDSUSP] = '\031';  /* ^Y */

#endif
#if defined(VREPRINT)
    termios.c_cc[VREPRINT] = '\022'; /* ^R */

#endif
    termios.c_cc[VLNEXT] = '\017';  /* ^V */
    termios.c_cc[VINTR]  = termOpt[0];
    termios.c_cc[VQUIT]  = termOpt[1];
    termios.c_cc[VERASE] = termOpt[2];
    termios.c_cc[VKILL]  = termOpt[3];
    termios.c_cc[VEOF]   = termOpt[4];
    termios.c_cc[VSUSP]  = termOpt[5];

    return tcsetattr(pty, TCSANOW, &termios);
}

int setWindowSize(char *windowSize, int pty)
{
    struct winsize wsize;

    if ((sscanf(windowSize, "WINSIZE %hu,%hu,%hu,%hu", &wsize.ws_row,
	    &wsize.ws_col, &wsize.ws_xpixel, &wsize.ws_ypixel)) != 4) {
	mlog("%s: invalid winsize received\n", __func__);
	return -1;
    }

    if (ioctl(pty, TIOCSWINSZ, &wsize) < 0) {
	mlog("%s: ioctl(TIOCSWINSZ) failed\n", __func__);
	return -1;
    }

    return 0;
}

/**
 * @brief Add a new X11 client.
 *
 * Every X11 application will need a new connection to qsub to forward
 * the corresponding data. The connections is established here.
 *
 * @param fd The file descriptor were a new X11 client has connected.
 *
 * @param info Not used, but required by the selector facility.
 *
 * @param return Always return 0.
 */
static int handleNewX11Client(int fd, void *info)
{
    unsigned int clientlen;
    struct sockaddr_in SAddr;
    int socket = -1;
    ComHandle_t *com, *qcom;
    Job_t *job;
    Job_Conn_t *con;

    /* accept new tcp connection */
    clientlen = sizeof(SAddr);

    job = info;

    if ((socket = accept(fd, (void *)&SAddr, &clientlen)) == -1) {
	mlog("%s error accepting new x11 client on '%i' : %s\n", __func__,
		fd, strerror(errno));
	return 0;
    }

    if ((qcom = getQsubConnection(job)) == NULL) {
	mlog("%s: connecting to qsub failed\n", __func__);
	return 0;
    }

    /* register new connection */
    com = getComHandle(socket, TCP_PROTOCOL);
    Selector_register(socket, forwardX11ClientData, NULL);

    con = addJobConn(job, qcom, JOB_CON_X11_CLIENT);
    addJobConnF(con, com);

    /*
    mlog("%s: accepting new x11 client '%i' qsock: '%i'\n", __func__,
	socket, qcom->socket);
    */

    return 0;
}

int initX11Forwarding1(Job_t *job, int *xport)
{
    unsigned int port;
    char x11Port[50];
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int sock, s, sockok = 0;
    ComHandle_t *com;

    /* find free forwarding socket */
    for (port = X11_PORT_RANGE_START; port <= X11_PORT_RANGE_END; port++) {

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	snprintf(x11Port, sizeof(x11Port), "%u", 6000 + port);

	if ((s = getaddrinfo(NULL, x11Port, &hints, &result)) != 0) {
	    fprintf(stderr, "%s: getaddrinfo: %s\n", __func__, gai_strerror(s));
	    return -1;
	}

	for(rp = result; rp != NULL; rp = rp->ai_next) {
	    sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
	    /* skip reserved sockets */
	    if (sock <= 3) continue;

	    if ((bind(sock, rp->ai_addr, rp->ai_addrlen)) != 0) {
		mlog("%s: bind failed on '%i' : %s\n", __func__, sock,
			strerror(errno));
		close(sock);
		continue;
	    }

	    if ((listen(sock, 20)) != 0) {
		mlog("%s: listen failed on '%i' : %s\n", __func__, sock,
			strerror(errno));
		close(sock);
		continue;
	    } else {
		sockok = 1;
		break;
	    }
	}
	if (sockok) break;
    }
    freeaddrinfo(result);

    if (!sockok) return -1;

    /* first listen() is lying, lets try it again */
    if ((listen(sock, 20)) != 0) {
	fprintf(stderr, "%s: listen failed on '%i' : %s", __func__, sock,
		strerror(errno));
	return -1;
    }

    /* register the socket */
    com = getComHandle(sock, TCP_PROTOCOL);
    Selector_register(sock, handleNewX11Client, job);
    addJobConn(job, com, JOB_CON_X11_LISTEN);

    /*
    mlog("%s: register x11 client sock '%i' port '%i'\n", __func__, sock, port);
    */

    *xport = port;
    return 0;
}

int initX11Forwarding2(Job_t *job, char *x11Cookie, int port,
    char *display, size_t displaySize)
{
    unsigned int x11Screen;
    char x11Proto[500], x11Data[500], x11Display[100];
    char xauthCmd[200], x11Auth[100];
    FILE *fp;

    /* read magic cookie */
    if ((sscanf(x11Cookie, "%499[^:]:%499[^:]:%u", x11Proto, x11Data,
	    &x11Screen)) != 3) {
	mlog("%s: error reading x11 magic cookie\n", __func__);
	return -1;
    }

    /* set display var */
    snprintf(x11Display, sizeof(x11Display), "localhost:%u.%u", port, x11Screen);
    setenv("DISPLAY", x11Display, 1);
    unsetenv("XAUTHORITY");
    snprintf(display, displaySize, "DISPLAY=localhost:%u.%u", port, x11Screen);

    /* call xauth */
    snprintf(x11Auth, sizeof(x11Auth), "unix:%u.%u", port, x11Screen);
    snprintf(xauthCmd, sizeof(xauthCmd), "%s -q -", X11_AUTH_CMD);

    if ((fp = popen(xauthCmd, "w")) != NULL) {
	fprintf(fp, "remove %s\n", x11Auth);
	fprintf(fp, "add %s %s %s\n", x11Auth, x11Proto, x11Data);
	pclose(fp);
    } else {
	mlog("%s: open xauth '%s' failed\n", __func__, X11_AUTH_CMD);
	unsetenv("DISPLAY");
	display[0] = '\0';
	return -1;
    }
    return 0;
}
