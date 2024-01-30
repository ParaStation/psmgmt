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

#include <json-c/json.h>
#include <stdbool.h>

typedef struct psjson * psjson_t;

psjson_t jsonFromFile(const char *path);

bool jsonWalkPath(psjson_t psjson, const char *path);

const char *jsonGetString(psjson_t psjson, const char *path);

bool jsonDestroy(psjson_t psjson);

#endif  /* __PLUGIN_JSON */
