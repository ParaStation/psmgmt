/*
 *               ParaStation3
 * info.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: info.h,v 1.6 2002/01/30 10:09:35 eicker Exp $
 *
 */
/**
 * @file
 * info: Functions for information retrieving from ParaStation daemon
 *
 * $Id: info.h,v 1.6 2002/01/30 10:09:35 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __INFO_H
#define __INFO_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

int INFO_request_rdpstatus(int nodeno, void* buffer, int size);

int INFO_request_mcaststatus(int nodeno, void* buffer, int size);

int INFO_request_countstatus(int nodeno, void* buffer, int size);

int INFO_request_hoststatus(void* buffer, int size);

int INFO_request_host(unsigned int addr);

typedef struct {
    short nodeno;
    long tid;
    long ptid;
    uid_t uid;
    long group;
} INFO_taskinfo_t;

int INFO_request_tasklist(int nodeno, INFO_taskinfo_t taskinfo[], int size);

/**
 * Type of taskinfo request
 */

typedef enum {
    INFO_GETINFO = 0x01,    /**< get infos of this task (internally used) */
    INFO_ISALIVE = 0x02,    /**< check if the tid is alive */
    INFO_PTID = 0x03,       /**< get the parents TID */
    INFO_UID = 0x04         /**< get the uid of the task */
} INFO_info_t;

long INFO_request_taskinfo(long tid, INFO_info_t what);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __INFO_H */
