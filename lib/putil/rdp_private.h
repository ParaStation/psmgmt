/*
 *               ParaStation3
 * rdp_private.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: rdp_private.h,v 1.2 2002/01/15 11:45:32 eicker Exp $
 *
 */
/**
 * \file
 * rdp_private: Reliable Datagram Protocol for ParaStation daemon
 *              Private functions and definitions
 *
 * $Id: rdp_private.h,v 1.2 2002/01/15 11:45:32 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __RDP_PRIVATE_H
#define __RDP_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#if 0
} // <- just for emacs indentation
#endif
#endif

static void resendMsgs(int node);

static void closeConnection(int node);

static void handleMCAST(void);

static void MCAST_PING(RDPState state);

static void initMCAST(int group, unsigned short port);

static char *CIstate(RDPState state);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __RDP_H */
