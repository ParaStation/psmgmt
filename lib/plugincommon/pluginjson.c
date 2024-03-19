/*
 * ParaStation
 *
 * Copyright (C) 2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#if HAVE_JSON_C_DEVEL
#include <json-c/json.h>
#endif

#include <sys/mman.h>
#include <string.h>

#include "pluginjson.h"

#include "pluginhelper.h"
#include "pluginlog.h"
#include "pluginmalloc.h"

#define PSJSON_MAGIC 0x8124030297715469

/** Structure holding a json object */
struct psjson {
    long unsigned magic;
#if HAVE_JSON_C_DEVEL
    struct json_object *obj;
    struct json_object *pos;
#endif
    size_t objLen;
    char *path;
    char *mapMem;
};

static inline bool checkMagic(psjson_t psjson, const char *caller,
			      const int line)
{
    if (!psjson) {
	pluginlog("%s(%s@%d): invalid psjson\n", __func__, caller, line);
	return false;
    }
    if (psjson->magic != PSJSON_MAGIC) {
	pluginlog("%s(%s@%d): invalid magic %lu\n", __func__, caller, line,
		  psjson->magic);
	return false;
    }
    return true;
}

#if HAVE_JSON_C_DEVEL
static json_object *newObject(PS_JsonType_t type, const void *val)
{
    if (!val && (type != PSJSON_OBJECT && type != PSJSON_ARRAY)) return NULL;

    struct json_object *newObj = NULL;
    switch (type) {
	case PSJSON_STRING:
	    newObj = json_object_new_string((const char *) val);
	    break;
	case PSJSON_BOOL:
	    newObj = json_object_new_boolean(*(bool*) val);
	    break;
	case PSJSON_INT32:
	    newObj = json_object_new_int(*(int32_t*) val);
	    break;
	case PSJSON_INT64:
	    newObj = json_object_new_int64(*(int64_t*) val);
	    break;
	case PSJSON_DOUBLE:
	    newObj = json_object_new_double(*(double*) val);
	    break;
	case PSJSON_OBJECT:
	    newObj = json_object_new_object();
	    break;
	case PSJSON_ARRAY:
	    newObj = json_object_new_array();
	    break;
	default:
	    return NULL;
    }

    return newObj;
}
#endif

#if HAVE_JSON_C_DEVEL
static psjson_t doCreateObject(struct json_object *obj)
{
    psjson_t psjson = ucalloc(sizeof(*psjson));
    if (psjson) {
	psjson->magic = PSJSON_MAGIC;
	psjson->obj = obj;
	psjson->pos = obj;
    }
    return psjson;
}
#endif

psjson_t jsonNewObject(void)
{
#if HAVE_JSON_C_DEVEL
    struct json_object *obj = json_object_new_object();
    return doCreateObject(obj);
#else
    pluginflog("json-c support not available\n");
    return NULL;
#endif
}

psjson_t jsonFromFile(const char *path)
{
    if (!path) {
	pluginflog("invalid path\n");
	return NULL;
    }

#if HAVE_JSON_C_DEVEL

    size_t len;
    char *mapMem = mmapFile(path, &len);
    if (!mapMem) {
	pluginflog("failed to open '%s'\n", path);
	return NULL;
    }

    struct json_object *obj = json_tokener_parse(mapMem);
    if (!obj) {
	pluginflog("failed to parse '%s'\n", path);
	munmap(mapMem, len);
	return NULL;
    }

    psjson_t psjson = doCreateObject(obj);
    if (psjson) {
	psjson->objLen = len;
	psjson->mapMem = mapMem;
	psjson->path = ustrdup(path);
    } else {
	munmap(mapMem, len);
    }
    return psjson;
#else
    pluginflog("json-c support not available\n");
    return NULL;
#endif
}

#if HAVE_JSON_C_DEVEL
static struct json_object *handleArray(psjson_t psjson, char *next,
				       bool addMissing, bool silent,
				       const char *caller, const int line)
{
    struct json_object *nextObj = NULL;
    int32_t idx = -1;
    char *array = strchr(next, '[');
    if (array[1] != ']') {
	if (sscanf(next, "[%u]", &idx) != 1) {
	    if (!silent) {
		pluginflog("%s(%s@%d) invalid array definition\n",
			   __func__, caller, line);
	    }
	    return NULL;
	}

	if (!json_object_is_type(psjson->pos, json_type_array)) {
	    if (!silent) {
		pluginlog("%s(%s@%d): %s requested from non array\n",
			  __func__, caller, line, next);
	    }
	    return NULL;
	}
	nextObj = json_object_array_get_idx(psjson->pos, idx);
    }

    if (!nextObj) {
	if (!addMissing) {
	    if (!silent) {
		pluginlog("%s(%s@%d): error: %s not found\n", __func__,
			  caller, line, next);
	    }
	    return NULL;
	}

	if (!json_object_is_type(psjson->pos, json_type_array)) {
	    if (!silent) {
		pluginlog("%s(%s@%d): cannot add into non array object\n",
			  __func__, caller, line);
	    }
	    return NULL;
	}

	/* create missing object */
	nextObj = json_object_new_object();

	if (idx == -1) {
	    /* add object to end of array */
	    json_object_array_add(psjson->pos, nextObj);
	} else {
	    json_object_array_put_idx(psjson->pos, idx, nextObj);
	}
    }

    return nextObj;
}
#endif

bool __jsonWalkPath(psjson_t psjson, const char *path, bool addMissing,
		    bool silent, const char *caller, const int line)
{
    if (!checkMagic(psjson, caller, line)) return false;

    if (!path) {
	if (!silent) {
	    pluginlog("%s(%s@%d): invalid path\n", __func__, caller, line);
	}
	return false;
    }

#if HAVE_JSON_C_DEVEL

    /* set starting point to root object for absolute path */
    if (path[0] == '/') psjson->pos = psjson->obj;

    char *toksave, *next;
    const char delimiters[] = "/";
    char *dup = ustrdup(path);

    next = strtok_r(dup, delimiters, &toksave);
    while (next) {
	struct json_object *nextObj = NULL;

	char *array = strchr(next, '[');
	if (array) {
	    nextObj = handleArray(psjson, next, addMissing, silent,
				  caller, line);
	    if (!nextObj) {
		ufree(dup);
		return false;
	    }
	}

	if (!array && !json_object_object_get_ex(psjson->pos, next, &nextObj)) {
	    if (!addMissing) {
		if (!silent) {
		    pluginlog("%s(%s@%d): error: %s not found\n", __func__,
			      caller, line, next);
		}
		ufree(dup);
		return false;
	    }

	    /* create missing object */
	    nextObj = json_object_new_object();
	    json_object_object_add(psjson->pos, next, nextObj);
	}

	plugindbg(PLUGIN_LOG_JSON, "%s(%s@%d): walked to '%s'\n", __func__,
		  caller, line, next);

	psjson->pos = nextObj;
	next = strtok_r(NULL, delimiters, &toksave);
    }
    ufree(dup);

    return true;

#else
    pluginlog("%s(%s@%d): json-c support not available\n", __func__, caller,
	      line);
    return false;
#endif
}

const char *__jsonGetString(psjson_t psjson, const char *path,
			    const char *caller, const int line)
{
    if (!checkMagic(psjson, caller, line)) return false;

#if HAVE_JSON_C_DEVEL

    if (path) {
	if (!__jsonWalkPath(psjson, path, false, false, caller, line)) {
	    pluginlog("%s(%s@%d): failed to find %s in %s\n", __func__, caller,
		      line, path, psjson->path);
	    return NULL;
	}
    }

    return json_object_get_string(psjson->pos);

#else
    pluginlog("%s(%s@%d): json-c support not available\n", __func__, caller,
	      line);
    return NULL;
#endif
}

bool jsonGet(psjson_t psjson, const char *path, void *val,
	     PS_JsonType_t type, const char *caller, const int line)
{
    if (!checkMagic(psjson, caller, line)) return false;

    if (!val) {
	pluginlog("%s(%s@%d): invalid value given\n", __func__, caller, line);
	return false;
    }

#if HAVE_JSON_C_DEVEL

    if (path) {
	if (!__jsonWalkPath(psjson, path, false, false, caller, line)) {
	    pluginlog("%s(%s@%d): failed to find %s in %s\n", __func__, caller,
		      line, path, psjson->path);
	    return NULL;
	}
    }

    switch (type) {
	case PSJSON_BOOL:
	    *(bool *)val = json_object_get_boolean(psjson->pos);
	    break;
	case PSJSON_INT32:
	    *(uint32_t*)val = json_object_get_int(psjson->pos);
	    break;
	case PSJSON_INT64:
	    *(uint64_t*)val = json_object_get_int64(psjson->pos);
	    break;
	case PSJSON_DOUBLE:
	    *(double*)val = json_object_get_double(psjson->pos);
	    break;
	default:
	    pluginlog("%s(%s@%d): unknown json type %i\n", __func__, caller,
		      line, type);
	    return false;
    }

    return true;

#else
    pluginlog("%s(%s@%d): json-c support not available\n", __func__, caller,
	      line);
    return NULL;
#endif
}

bool jsonDestroy(psjson_t psjson)
{
    if (!checkMagic(psjson, __func__, __LINE__)) return false;

#if HAVE_JSON_C_DEVEL

    ufree(psjson->path);

    if (psjson->mapMem && psjson->objLen) {
	munmap(psjson->mapMem, psjson->objLen);
    }
    /* decrement reference count and free if it reaches zero */
    if (psjson->obj) json_object_put(psjson->obj);

    psjson->magic = 0;

    free(psjson);

    return true;

#else
    pluginflog("json-c support not available\n");
    return false;
#endif
}

bool jsonIsAvail(void)
{
#if HAVE_JSON_C_DEVEL
    return true;
#else
    return false;
#endif
}

bool jsonWriteFile(psjson_t psjson, const char *dir, const char *filename)
{
    if (!checkMagic(psjson, __func__, __LINE__)) return false;

    if (!filename) {
	pluginflog("invalid filename \n");
	return false;
    }

    if (!dir) {
	pluginflog("invalid directory\n");
	return false;
    }

#if HAVE_JSON_C_DEVEL

    const char* strObj = json_object_to_json_string_ext(psjson->obj,
			    JSON_C_TO_STRING_PRETTY);
    if (!strObj || strlen(strObj) < 1) {
	pluginflog("converting object to string failed\n");
	return false;
    }

    if (!writeFile(filename, dir, strObj, strlen(strObj))) {
	pluginflog("writing object failed\n");
	return false;
    }

    return true;
#else
    pluginflog("json-c support not available\n");
    return false;
#endif
}

bool jsonPut(psjson_t psjson, const char *path, const char *key,
	     const void *val, PS_JsonType_t type, const char *caller,
	     const int line)
{
    if (!checkMagic(psjson, caller, line)) return false;

    if ((type != PSJSON_OBJECT && type != PSJSON_ARRAY) && !val) {
	pluginlog("%s(%s@%d): invalid value given\n", __func__, caller, line);
	return false;
    }

#if HAVE_JSON_C_DEVEL

    if (path) {
	if (!__jsonWalkPath(psjson, path, true, false, caller, line)) {
	    pluginlog("%s(%s@%d): failed to find %s in %s\n", __func__, caller,
		      line, path, psjson->path);
	    return NULL;
	}
    }

    struct json_object *newObj = newObject(type, val);
    if (!newObj) {
	pluginlog("%s(%s@%d): failed to create new json object %s\n", __func__,
		  caller, line, key);
	return false;
    }

    /* save new object to previous walked path */
    if (!json_object_is_type(psjson->pos, json_type_array)) {
	if (!key) {
	    pluginlog("%s(%s@%d): path is not an array and no key given\n",
		      __func__, caller, line);
	    return false;
	}
	json_object_object_add(psjson->pos, key, newObj);
    } else {
	if (key) {
	    pluginlog("%s(%s@%d): path is an array and key %s was given\n",
		      __func__, caller, line, key);
	    return false;
	}
	json_object_array_add(psjson->pos, newObj);
    }

    return true;
#else
    pluginlog("%s(%s@%d): json-c support not available\n", __func__, caller,
	      line);
    return false;
#endif
}

int __jsonArrayLen(psjson_t psjson, const char *path,
		   const char *caller, const int line)
{
    if (!checkMagic(psjson, caller, line)) return false;

#if HAVE_JSON_C_DEVEL

    if (path) {
	if (!__jsonWalkPath(psjson, path, false, false, caller, line)) {
	    pluginlog("%s(%s@%d): failed to find %s in %s\n", __func__, caller,
		      line, path, psjson->path);
	    return -1;
	}
    }

    if (!json_object_is_type(psjson->pos, json_type_array)) {
	plugindbg(PLUGIN_LOG_JSON, "%s(%s@%d) %s is not an array\n", __func__,
		  caller, line, (path ? path : "position"));
	return -1;
    }

    return json_object_array_length(psjson->pos);

#else
    pluginflog("(%s(%s@%d): json-c support not available\n", __func__, caller,
	       line);
    return -1;
#endif
}

bool __jsonDel(psjson_t psjson, const char *path, const char *key,
	       const char *caller, const int line)
{
    if (!checkMagic(psjson, caller, line)) return false;

#if HAVE_JSON_C_DEVEL
    if (path) {
	if (!__jsonWalkPath(psjson, path, false, false, caller, line)) {
	    pluginlog("%s(%s@%d): failed to find %s in %s\n", __func__, caller,
		      line, path, psjson->path);
	    return false;
	}
    }

    json_object_object_del(psjson->pos, key);
    return true;

#else
    pluginflog("json-c support not available\n");
    return false;
#endif
}
