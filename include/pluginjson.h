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

/** json object types */
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

/** json context to be created via @ref jsonNewObject() */
typedef struct psjson * psjson_t;

/**
 * @brief Create a new json context
 *
 * @return Returns a new json context on success otherwise NULL
 * is returned
 */
psjson_t jsonNewObject(void);

/**
 * @brief Create a json context from a json file
 *
 * Maps the give json file to memory and creates a new json context from it.
 *
 * @param path Path to the json file to read
 *
 * @return Returns a new json context on success otherwise
 * NULL is returned
 */
psjson_t jsonFromFile(const char *path);

/**
 * @brief Change current position of json context
 *
 * @param psjson json context to change
 *
 * @param path New relative or absolute path to change to
 *
 * @param addMissing Add missing objects while walking the path
 *
 * @param silent Suppress error messages
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns true on success otherwise false is returned
 */
bool __jsonWalkPath(psjson_t psjson, const char *path, bool addMissing,
		    bool silent, const char *caller, const int line);

#define jsonWalkPath(psjson, path)				    \
    __jsonWalkPath(psjson, path, false, false,			    \
		    __func__, __LINE__)

#define jsonExists(psjson, path)				    \
    __jsonWalkPath(psjson, path, false, true, __func__, __LINE__)

/**
 * @brief Read string from json context
 *
 * Read a string object from a json context. If an optional @a path is specified
 * the position pointer of json context is changed before the object gets
 * red. If @ref path is NULL the current position is used.
 *
 * @param psjson json context to read from
 *
 * @param path Optional path to string object
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 */
const char *__jsonGetString(psjson_t psjson, const char *path,
			    const char *caller, const int line);

#define jsonGetString(psjson, path)				    \
    __jsonGetString(psjson, path, __func__, __LINE__)

/**
 * @brief Read object from json context
 *
 * Read a object of specific @a type from a json context.
 * If an optional @a path is specified the position pointer of json context
 * is changed before the object gets red. If @ref path is NULL the
 * current position is used.
 *
 * @param psjson json context to read from
 *
 * @param path Optional path to object to read
 *
 * @param val Value will hold the result on success
 *
 * @param type Type of the object to read
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns true on success otherwise false is returned
 */
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

/**
 * @brief Add new object to json context
 *
 * Add a new object of specified @a type to json context. If an optional
 * @a path is specified the position pointer of json context
 * is changed before the object gets added. If @ref path is NULL the
 * current position is used. The new object is added only in memory.
 * To serialize the changes use @ref jsonWriteFile().
 *
 * @param path Optional path to object to write
 *
 * @param key Name of the object to write (type dependent might be optional)
 *
 * @param val Value of the object to write (type dependent might be optional)
 *
 * @param type Type of the object to write
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns true on success otherwise false is returned
 */
bool jsonPut(psjson_t psjson, const char *path, const char *key,
	     const void *val, PS_JsonType_t type, const char *caller,
	     const int line);

#define jsonPutString(psjson, key, val)				    \
	jsonPut(psjson, NULL, key, val, PSJSON_STRING,		    \
	    __func__, __LINE__)

#define jsonPutStringP(psjson, path, key, val)			    \
	jsonPut(psjson, path, key, val, PSJSON_STRING,		    \
	    __func__, __LINE__)

#define jsonPutArray(psjson, key)				    \
	jsonPut(psjson, NULL, key, NULL, PSJSON_ARRAY,		    \
	    __func__, __LINE__)

#define jsonPutArrayP(psjson, path, key, val)			    \
	jsonPut(psjson, path, key, val, PSJSON_ARRAY,		    \
	    __func__, __LINE__)

#define jsonPutBool(psjson, key, val) { bool _x = val;		    \
	jsonPut(psjson, NULL, key, &_x, PSJSON_BOOL,		    \
	    __func__, __LINE__); }

#define jsonPutBoolP(psjson, path, key, val) { bool _x = val;	    \
	jsonPut(psjson, path, key, &_x, PSJSON_BOOL,		    \
	    __func__, __LINE__); }

#define jsonPutInt32(psjson, key, val) { int32_t _x = val;	    \
	jsonPut(psjson, NULL, key, &_x, PSJSON_INT32,		    \
	    __func__, __LINE__); }

#define jsonPutInt32P(psjson, path, key, val) { int32_t _x = val;   \
	jsonPut(psjson, path, key, &_x, PSJSON_INT32,		    \
	    __func__, __LINE__); }

#define jsonPutInt64(psjson, key, val) { int64_t _x = val;	    \
	jsonPut(psjson, NULL, key, &_x, PSJSON_INT64,		    \
	    __func__, __LINE__); }

#define jsonPutInt64P(psjson, path, key, val) { int64_t _x = val;   \
	jsonPut(psjson, path, key, &_x, PSJSON_INT64,		    \
	    __func__, __LINE__); }

#define jsonPutDouble(psjson, key, val) { double _x = val;	    \
	jsonPut(psjson, NULL, key, &_x, PSJSON_DOUBLE,		    \
	    __func__, __LINE__); }

#define jsonPutDoubleP(psjson, path, key, val) { double _x = val;   \
	jsonPut(psjson, path, key, &_x, PSJSON_DOUBLE,		    \
	    __func__, __LINE__); }

/**
 * @brief Get the number of entries of a json array object
 *
 * @param psjson json context holding the array object
 *
 * @param path Optional path to the array object
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns the number of entries on success otherwise -1 is
 * returned
 */
int __jsonArrayLen(psjson_t psjson, const char *path,
		   const char *caller, const int line);

#define jsonArrayLen(psjson, path)				    \
    __jsonArrayLen(psjson, path, __func__, __LINE__)

/**
 * @brief Destroy a json context and free all memory
 *
 * @param psjson json context to destroy
 *
 * @return Returns true if destruction was successful otherwise
 * false is returned
 */
bool jsonDestroy(psjson_t psjson);

/**
 * @brief Test if json support is available
 *
 * Check if json support is available.
 *
 * @return Returns true if json support is available otherwise
 * false is returned
 */
bool jsonIsAvail(void);

/**
 * @brief Write all json objects to file
 *
 * @param psjson json context holding objects to write
 *
 * @param dir Directory where the file should be written to
 *
 * @param name Name of the file
 *
 * @return Returns true if the file was successfully written or false
 * otherwise
 */
bool jsonWriteFile(psjson_t psjson, const char *dir, const char *filename);

/**
 * @brief Delete object from json context
 *
 * Delete a json object including all sub-objects attached to it. If an optional
 * @a path is specified the position pointer of json context is changed before
 * the objects gets deleted. If @ref path is NULL the current position is used.
 * The object is removed only in memory. To serialize the changes use
 * @ref jsonWriteFile().
 *
 * @param psjson json context holding objects to delete
 *
 * @param path Optional path to object to delete
 *
 * @param key Name of the object to delete
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns true if the object was successfully removed or false
 * otherwise
 */
bool __jsonDel(psjson_t psjson, const char *path, const char *key,
	       const char *caller, const int line);

#define jsonDel(psjson, path, key)				    \
    __jsonDel(psjson, path, key, __func__, __LINE__)

#endif  /* __PLUGIN_JSON */
