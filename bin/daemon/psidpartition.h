/*
 *               ParaStation
 * psidpartition.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidpartition.h,v 1.2 2004/01/09 16:05:36 eicker Exp $
 *
 */
/**
 * @file
 * Helper functions in order to setup and handle partitions.
 *
 * $Id: psidpartition.h,v 1.2 2004/01/09 16:05:36 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDPARTITION_H
#define __PSIDPARTITION_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Handle a PSP_CD_CREATEPART message.
 *
 * Handle the message @a inmsg of type PSP_CD_CREATEPART.
 *
 * @doctodo
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_CREATEPART(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_CD_CREATEPARTNL message.
 *
 * Handle the message @a inmsg of type PSP_CD_CREATEPARTNL.
 *
 * @doctodo
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_CREATEPARTNL(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_CD_GETPART message.
 *
 * Handle the message @a inmsg of type PSP_CD_GETPART.
 *
 * @doctodo
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_GETPART(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_CD_GETPARTNL message.
 *
 * Handle the message @a inmsg of type PSP_CD_GETPARTNL.
 *
 * @doctodo
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_GETPARTNL(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_CD_PROVIDEPART message.
 *
 * Handle the message @a inmsg of type PSP_CD_PROVIDEPART.
 *
 * @doctodo
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_PROVIDEPART(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_CD_PROVIDEPARTNL message.
 *
 * Handle the message @a inmsg of type PSP_CD_PROVIDEPARTNL.
 *
 * @doctodo
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_PROVIDEPARTNL(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_CD_GETNODES message.
 *
 * Handle the message @a inmsg of type PSP_CD_GETNODES.
 *
 * @doctodo
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_GETNODES(DDBufferMsg_t *inmsg);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDPARTITION_H */
