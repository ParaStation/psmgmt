#ifndef __LOGGER_H__
#define __LOGGER_H__

#include <sys/types.h>
#include "psitask.h"

extern int PrependSource;

extern fd_set LOGGERmyfds;
extern int LOGGERnoclients;

/*************************************************************
 * int
 * LOGGERredirect_std(u_long port)
 * 
 * redirect stdout/stderr to sockets connected to port
 * RETURN 0 success
 *        -1 error errno is set.
 */
int LOGGERredirect_std(unsigned int node, int port, PStask_t* task);

/*********************************************************************
 * LOGGERgetparentsock(int listensock)
 *
 * waits util the parent connects to this socket.
 * This socket is needed for (logger<->parent) control
 */
int LOGGERgetparentsock(int listensock);

/*********************************************************************
 * LOGGERloop(LOGGERlisten,LOGGERparent)
 *
 * does all the logging work. The connection to the parent is already
 * installed. Now childs can connect and log out via the logger.
 */
int LOGGERloop(int LOGGERlisten,int LOGGERparent);

/*********************************************************************
 * long
 * LOGGERspawnlogger()
 *
 * spawns a logger and creates a channel to it.
 * RETURN the portno of the logger
 */
long LOGGERspawnlogger();

#endif
