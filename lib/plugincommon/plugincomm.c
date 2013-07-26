/*
 * ParaStation
 *
 * Copyright (C) 2012 - 2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>

#include "pluginmalloc.h"
#include "pluginlog.h"
#include "psidcomm.h"

#include "plugincomm.h"

int debug = 0;

/**
 * @brief Grow the data buffer if needed.
 *
 * @param len The additional size needed.
 *
 * @param data Pointer to the actual data buffer.
 *
 * @return No return value.
 */
static void growBuffer(size_t len, PS_DataBuffer_t *data)
{
    if (data->buf == NULL) {
	data->buf = umalloc(BufTypedMsgSize);
	data->bufSize = BufTypedMsgSize;
	data->bufUsed = 0;
    }

    while (data->bufUsed + len > data->bufSize) {
	data->buf = urealloc(data->buf, data->bufSize + BufTypedMsgSize);
	data->bufSize += BufTypedMsgSize;
    }
}

int __addStringToMsg(const char *string, PS_DataBuffer_t *data,
			const char *caller)
{
    size_t len;
    char *ptr;

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    len = (!string) ? 0 : strlen(string);

    growBuffer(sizeof(uint8_t) + sizeof(int32_t) + len, data);
    ptr = data->buf + data->bufUsed;

    /* add data type */
    *(uint8_t *)ptr = PSDATA_STRING;
    ptr += sizeof(uint8_t);
    data->bufUsed +=sizeof(uint8_t);

    /* string length */
    *(int32_t *)ptr = len;
    ptr += sizeof(int32_t);
    data->bufUsed += sizeof(int32_t);

    /* add string itself */
    if (len > 0) {
	memcpy(ptr, string, len);
	ptr += len;
	data->bufUsed += len;
    }

    return 1;
}

int __addInt32ToMsg(const int32_t *val, PS_DataBuffer_t *data,
		    const char *caller)
{
    char *ptr;

    if (!val) {
	pluginlog("%s: invalid value from '%s'\n", __func__, caller);
	return 0;
    }

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    growBuffer(sizeof(uint8_t) + sizeof(int32_t), data);
    ptr = data->buf + data->bufUsed;

    /* add data type */
    *(uint8_t *)ptr = PSDATA_INT32;
    ptr += sizeof(uint8_t);
    data->bufUsed += sizeof(uint8_t);

    /* add result */
    *(int32_t *) ptr = *(int32_t *) val;
    ptr += sizeof(int32_t);
    data->bufUsed += sizeof(int32_t);

    return 1;
}

int __addTimeToMsg(const time_t *time, PS_DataBuffer_t *data,
		    const char *caller)
{
    char *ptr;

    if (!time) {
	pluginlog("%s: invalid time from '%s'\n", __func__, caller);
	return 0;
    }

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    growBuffer(sizeof(uint8_t) + sizeof(uint64_t), data);
    ptr = data->buf + data->bufUsed;

    /* add data type */
    *(uint8_t *)ptr = PSDATA_TIME;
    ptr += sizeof(uint8_t);
    data->bufUsed += sizeof(uint8_t);

    *(uint64_t *) ptr = *(time_t *) time;
    ptr += sizeof(uint64_t);
    data->bufUsed += sizeof(uint64_t);

    return 1;
}

int __addPidToMsg(const pid_t *pid, PS_DataBuffer_t *data, const char *caller)
{
    char *ptr;

    if (!pid) {
	pluginlog("%s: invalid pid from '%s'\n", __func__, caller);
	return 0;
    }

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    growBuffer(sizeof(uint8_t) + sizeof(pid_t), data);
    ptr = data->buf + data->bufUsed;

    /* add data type */
    *(uint8_t *)ptr = PSDATA_PID;
    ptr += sizeof(uint8_t);
    data->bufUsed += sizeof(uint8_t);

    *(pid_t *) ptr = *(pid_t *) pid;
    ptr += sizeof(pid_t);
    data->bufUsed += sizeof(pid_t);

    return 1;
}

int __addStringToMsgBuf(DDTypedBufferMsg_t *msg, char **ptr,
    const char *string, const char *caller)
{
    size_t len;

    if (!*ptr) {
	pluginlog("%s: invalid ptr from '%s'\n", __func__, caller);
	return 0;
    }

    if (!string) {
	len = 0;
    } else {
	len = strlen(string);
    }

    /* add data type */
    *(uint8_t *) *ptr = PSDATA_STRING;
    *ptr += sizeof(uint8_t);
    msg->header.len += sizeof(uint8_t);

    /* string length */
    *(int32_t *) *ptr = len;
    *ptr += sizeof(int32_t);
    msg->header.len += sizeof(int32_t);

    if (msg->header.len > BufTypedMsgSize) {
	pluginlog("%s: message buffer to small from '%s'!\n", __func__, caller);
	return 0;
    }

    /* add string itself */
    if (len > 0) {
	memcpy(*ptr, string, len);
	*ptr += len;
	msg->header.len += len;
    }

    /*
    pluginlog("adding buffer '%s' len '%zu'\n", string, len);
    */

    return 1;
}

char *__getStringFromMsgBufM(char **ptr, const char *caller)
{
    uint8_t type;
    char *data;
    size_t len;

    if (!*ptr) {
	if (debug) {
	    pluginlog("%s: invalid ptr from '%s'\n", __func__, caller);
	}
	return NULL;
    }

    /* verify data type */
    type = *(uint8_t *) *ptr;
    *ptr += sizeof(uint8_t);

    if (type != PSDATA_STRING) {
	if (debug) {
	    pluginlog("%s: protocol error got type '%i' should be '%i' from "
			"'%s'\n", __func__, type, PSDATA_STRING, caller);
	}
	return NULL;
    }

    /* string length */
    len = *(int32_t *) *ptr;
    *ptr += sizeof(int32_t);

    data = umalloc(len +1);

    /* extract the string */
    if (len > 0) {
	memcpy(data, *ptr, len);
	data[len] = '\0';
	*ptr += len;
    } else {
	data[0] = '\0';
    }

    return data;
}

char *__getStringFromMsgBuf(char **ptr, char *buf, size_t buflen,
	const char *caller)
{
    uint8_t type;
    size_t len;

    if (!buf) {
	pluginlog("%s: invalid buffer from '%s'\n", __func__, caller);
	return NULL;
    }
    buf[0] = '\0';

    if (!*ptr) {
	if (debug) {
	    pluginlog("%s: invalid ptr from '%s'\n", __func__, caller);
	}
	return NULL;
    }

    /* verify data type */
    type = *(uint8_t *) *ptr;
    *ptr += sizeof(uint8_t);

    if (type != PSDATA_STRING) {
	if (debug) {
	    pluginlog("%s: protocol error got type '%i' should be '%i' from "
			"'%s'\n", __func__, type, PSDATA_STRING, caller);
	}
	return NULL;
    }

    /* string length */
    len = *(int32_t *) *ptr;
    *ptr += sizeof(int32_t);

    //pluginlog("reading buffer len '%i'\n", len);

    /* buffer to small */
    if (len +1 > buflen) {
	pluginlog("%s: buffer (%zu) to small for message (%zu) from '%s'\n",
		__func__, buflen, len+1, caller);
	return NULL;
    }

    /* extract the string */
    if (len > 0) {
	memcpy(buf, *ptr, len);
	buf[len] = '\0';
	*ptr += len;
    } else {
	buf[0] = '\0';
    }

    return buf;
}

int __addTimeToMsgBuf(DDTypedBufferMsg_t *msg, char **ptr,
				time_t *time, const char *caller)
{
    if (!time) {
	pluginlog("%s: invalid time from '%s'\n", __func__, caller);
	return 0;
    }

    if (!*ptr) {
	pluginlog("%s: invalid ptr from '%s'\n", __func__, caller);
	return 0;
    }

    if ((msg->header.len + sizeof(uint64_t)) > BufTypedMsgSize) {
	pluginlog("%s: message buffer to small from '%s'\n", __func__, caller);
	return 0;
    }

    /* add data type */
    *(uint8_t *) *ptr = PSDATA_TIME;
    *ptr += sizeof(uint8_t);
    msg->header.len += sizeof(uint8_t);

    *(uint64_t *) *ptr = *(time_t *) time;
    *ptr += sizeof(uint64_t);
    msg->header.len += sizeof(uint64_t);

    return 1;
}

int __getTimeFromMsgBuf(char **ptr, time_t *time, const char *caller)
{
    uint8_t type;

    if (!*ptr) {
	pluginlog("%s: invalid ptr from '%s'\n", __func__, caller);
	return 0;
    }

    if (!time) {
	pluginlog("%s: invalid time from '%s'\n", __func__, caller);
	return 0;
    }

    /* verify data type */
    type = *(uint8_t *) *ptr;
    *ptr += sizeof(uint8_t);

    if (type != PSDATA_TIME) {
	pluginlog("%s: protocol error got type '%i' should be '%i'\n", __func__,
		type, PSDATA_TIME);
	return 0;
    }

    *time = *(uint64_t *) *ptr;
    *ptr += sizeof(uint64_t);

    return 1;
}

int __getPidFromMsgBuf(char **ptr, pid_t *pid, const char *caller)
{
    uint8_t type;

    if (!*ptr) {
	pluginlog("%s: invalid ptr from '%s'\n", __func__, caller);
	return 0;
    }

    if (!pid) {
	pluginlog("%s: invalid pid from '%s'\n", __func__, caller);
	return 0;
    }

    /* verify data type */
    type = *(uint8_t *) *ptr;
    *ptr += sizeof(uint8_t);

    if (type != PSDATA_PID) {
	pluginlog("%s: protocol error got type '%i' should be '%i'\n", __func__,
		type, PSDATA_PID);
	return 0;
    }

    *pid = *(pid_t *) *ptr;
    *ptr += sizeof(pid_t);

    return 1;
}

int __addInt32ToMsgBuf(DDTypedBufferMsg_t *msg, char **ptr,
			int32_t val, const char *caller)
{
    if (!msg) {
	pluginlog("%s: invalid msg from '%s'\n", __func__, caller);
	return 0;
    }

    if (!*ptr) {
	pluginlog("%s: invalid ptr from '%s'\n", __func__, caller);
	return 0;
    }

    /* add data type */
    *(uint8_t *) *ptr = PSDATA_INT32;
    *ptr += sizeof(uint8_t);
    msg->header.len += sizeof(uint8_t);

    /* add result */
    *(int32_t *) *ptr = val;
    *ptr += sizeof(int32_t);
    msg->header.len += sizeof(int32_t);

    return 1;
}

int __getInt32FromMsgBuf(char **ptr, int32_t *val, const char *caller)
{
    uint8_t type;

    if (!*ptr) {
	pluginlog("%s: invalid ptr from '%s'\n", __func__, caller);
	return 0;
    }

    if (!val) {
	pluginlog("%s: invalid value from '%s'\n", __func__, caller);
	return 0;
    }

    /* verify data type */
    type = *(uint8_t *) *ptr;
    *ptr += sizeof(uint8_t);

    if (type != PSDATA_INT32) {
	pluginlog("%s: protocol error got type '%i' should be '%i'\n",
		    __func__, type, PSDATA_STRING);
	return 0;
    }

    *val = *(int32_t *) *ptr;
    *ptr += sizeof(int32_t);

    return 1;
}
