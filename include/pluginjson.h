/*
 * ParaStation
 *
 * Copyright (C) 2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PLUGIN_JSON
#define __PLUGIN_JSON

#include <stdbool.h>

typedef enum {
    PSJSON_ERROR = 0,
    PSJSON_STRING = 0x03,
    PSJSON_BOOL,
    PSJSON_INT32,
    PSJSON_INT64,
    PSJSON_DOUBLE,
    PSJSON_ARRAY,
    PSJSON_OBJECT,
    PSJSON_NULL
} PS_JsonType_t;

typedef struct psjson * psjson_t;

psjson_t jsonNewObject(void);

psjson_t jsonFromFile(const char *path);

bool __jsonWalkPath(psjson_t psjson, const char *path, bool addMissing,
		    bool silent, const char *caller, const int line);

#define jsonWalkPath(psjson, path)				    \
    __jsonWalkPath(psjson, path, false, false,			    \
		    __func__, __LINE__)

#define jsonExists(psjson, path)				    \
    __jsonWalkPath(psjson, path, false, true, __func__, __LINE__)

const char *__jsonGetString(psjson_t psjson, const char *path,
			    const char *caller, const int line);

#define jsonGetString(psjson, path)				    \
    __jsonGetString(psjson, path, __func__, __LINE__)

bool jsonGet(psjson_t psjson, const char *path, void *val,
	     PS_JsonType_t type, const char *caller, const int line);

#define jsonGetBool(psjson, path, val) { bool *_x = val;	    \
	jsonGet(psjson, path, _x, PSJSON_BOOL,			    \
		   __func__, __LINE__); }

#define jsonGetInt32(psjson, path, val) { int32_t *_x = val;	    \
	jsonGet(psjson, path, _x, PSJSON_INT32,			    \
		   __func__, __LINE__); }

#define jsonGetInt64(psjson, path, val) { int64_t *_x = val;	    \
	jsonGet(psjson, path, _x, PSJSON_INT64,			    \
		   __func__, __LINE__); }

#define jsonGetDouble(psjson, path, val) { double *_x = val;	    \
	jsonGet(psjson, path, _x, PSJSON_DOUBLE,		    \
		   __func__, __LINE__); }

bool jsonPut(psjson_t psjson, const char *path, const char *key,
	     const void *val, PS_JsonType_t type, const char *caller,
	     const int line);

#define jsonPutString(psjson, path, key, val)			    \
	jsonPut(psjson, path, key, val, PSJSON_STRING,		    \
	    __func__, __LINE__)

#define jsonPutArray(psjson, path, key, val)			    \
	jsonPut(psjson, path, key, val, PSJSON_ARRAY,		    \
	    __func__, __LINE__)

#define jsonPutBool(psjson, path, key, val) { bool _x = val;	    \
	jsonPut(psjson, path, key, &_x, PSJSON_BOOL,		    \
	    __func__, __LINE__); }

#define jsonPutInt32(psjson, path, key, val) { int32_t _x = val;    \
	jsonPut(psjson, path, key, &_x, PSJSON_INT32,		    \
	    __func__, __LINE__); }

#define jsonPutInt64(psjson, path, key, val) { int64_t _x = val;    \
	jsonPut(psjson, path, key, &_x, PSJSON_INT64,		    \
	    __func__, __LINE__); }

#define jsonPutDouble(psjson, path, key, val) { double _x = val;    \
	jsonPut(psjson, path, key, &_x, PSJSON_DOUBLE,		    \
	    __func__, __LINE__); }

int __jsonArrayLen(psjson_t psjson, const char *path,
		   const char *caller, const int line);

#define jsonArrayLen(psjson, path)				    \
    __jsonArrayLen(psjson, path, __func__, __LINE__)

bool jsonDestroy(psjson_t psjson);

bool jsonIsAvail(void);

bool jsonWriteFile(psjson_t psjson, const char *dir, const char *filename);

#endif  /* __PLUGIN_JSON */
