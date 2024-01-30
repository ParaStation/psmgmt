/*
 * ParaStation
 *
 * Copyright (C) 2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#if JSON_C_ENABLED
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
#if JSON_C_ENABLED
    struct json_object *obj;
    struct json_object *pos;
#endif
    size_t objLen;
    char *path;
    char *mapMem;
};

psjson_t jsonFromFile(const char *path)
{
    if (!path) {
	pluginlog("%s: invalid path given\n", __func__);
	return NULL;
    }

#if JSON_C_ENABLED

    size_t len;
    char *mapMem = mmapFile(path, &len);
    if (!mapMem) {
	pluginlog("%s: failed to open %s\n", __func__, path);
	return NULL;
    }

    struct json_object *obj = json_tokener_parse(mapMem);
    if (!obj) {
	pluginlog("%s: failed to parse %s\n", __func__, path);
	munmap(mapMem, len);
	return NULL;
    }

    psjson_t psjson = umalloc(sizeof(psjson));
    psjson->objLen = len;
    psjson->obj = obj;
    psjson->pos = obj;
    psjson->mapMem = mapMem;
    psjson->magic = PSJSON_MAGIC;
    psjson->path = ustrdup(path);

    return psjson;

#else
    pluginlog("%s: json-c support not available\n", __func__);
    return NULL;
#endif
}

bool jsonWalkPath(psjson_t psjson, const char *path)
{
    if (!path) {
	pluginlog("%s: invalid path given\n", __func__);
	return false;
    }

#if JSON_C_ENABLED

    /* reset to beginning */
    psjson->pos = psjson->obj;

    char *toksave, *next;
    const char delimiters[] = "/";
    char *dup = ustrdup(path);

    next = strtok_r(dup, delimiters, &toksave);
    while (next) {
	if (!json_object_object_get_ex(psjson->pos, next, &psjson->pos)) {
	    pluginlog("%s: error: %s not found in %s\n", __func__, next,
		      psjson->path);
	    ufree(dup);
	    return false;
        }
	next = strtok_r(NULL, delimiters, &toksave);
    }
    ufree(dup);

    return true;

#else
    pluginlog("%s: json-c support not available\n", __func__);
    return false;
#endif
}

const char *jsonGetString(psjson_t psjson, const char *path)
{
    if (!path) {
	pluginlog("%s: invalid path given\n", __func__);
	return false;
    }

#if JSON_C_ENABLED
    if (!jsonWalkPath(psjson, path)) {
	pluginlog("%s: failed to find %s in %s\n", __func__, path,
		  psjson->path);
	return NULL;
    }

    const char *envVal = json_object_get_string(psjson->pos);

    return envVal;

#else
    pluginlog("%s: json-c support not available\n", __func__);
    return NULL;
#endif
}

bool jsonDestroy(psjson_t psjson)
{
    if (!psjson) {
	pluginlog("%s: invalid object given\n", __func__);
	return false;
    }

    if (psjson->magic != PSJSON_MAGIC) {
	pluginlog("%s: invalid magic of psjson\n", __func__);
	return false;
    }

#if JSON_C_ENABLED

    ufree(psjson->path);

    if (psjson->mapMem && psjson->objLen) {
	munmap(psjson->mapMem, psjson->objLen);
    }
    if (psjson->obj) json_object_put(psjson->obj);

    psjson->magic = 0;

    free(psjson);

    return true;

#else
    pluginlog("%s: json-c support not available\n", __func__);
    return false;
#endif
}
