/*
 *               ParaStation
 * psidpartition.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidpartition.h,v 1.1 2003/09/12 15:34:44 eicker Exp $
 *
 */
/**
 * @file
 * Helper functions in order to setup and handle partitions.
 *
 * $Id: psidpartition.h,v 1.1 2003/09/12 15:34:44 eicker Exp $
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
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_GETNODES(DDBufferMsg_t *inmsg);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDPARTITION_H */
