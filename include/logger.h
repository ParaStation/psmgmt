#ifndef __LOGGER_H__
#define __LOGGER_H__

#include <sys/types.h>
#include "psitask.h"

struct LOGGERclient_t{
    long id;
    int std;
};

/*************************************************************
 * int
 * LOGGERredirect_std(int port)
 * 
 * redirect stdout/stderr to sockets connected to port
 *
 * RETURN 0 success
 *        -1 error errno is set.
 */
int LOGGERredirect_std(unsigned int node, int port, PStask_t* task);

/*********************************************************************
 * int LOGGERspawnforwarder(unsigned int logger_node, int logger_port)
 *
 * spawns a forwarder. the forwarder will create a channel to the logger
 * listening at logger_node on logger_port.
 *
 * RETURN the portno of the forwarder
 */
int LOGGERspawnforwarder(unsigned int logger_node, int logger_port);

/*********************************************************************
 * int LOGGERspawnlogger()
 *
 * spawns a logger.
 *
 * RETURN the portno of the logger
 */
int LOGGERspawnlogger(void);

#endif
