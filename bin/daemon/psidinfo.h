/*
 *               ParaStation
 * psidinfo.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidinfo.h,v 1.2 2004/01/09 16:02:07 eicker Exp $
 *
 */
/**
 * @file
 * Handle info requests to the ParaStation daemon.
 *
 * $Id: psidinfo.h,v 1.2 2004/01/09 16:02:07 eicker Exp $
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
 * @doctodo
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
