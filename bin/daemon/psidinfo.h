/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * Handle info requests to the ParaStation daemon.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDINFO_H
#define __PSIDINFO_H

#include "psprotocol.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Handle a PSP_CD_INFOREQUEST message.
 *
 * Handle the message @a inmsg of type PSP_CD_INFOREQUEST.
 *
 * This kind of messages is used by client processes (actually most of
 * the time psiadmin processes) in order to get information on the
 * cluster and its current state. After retrieving the requested
 * information one or more PSP_CD_INFORESPONSE messages are generated
 * and send to the client process.
 *
 * Since some information is not available on every node of the
 * cluster, @a inmsg might be forwarded to further nodes.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_INFOREQUEST(DDTypedBufferMsg_t *inmsg);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDINFO_H */
