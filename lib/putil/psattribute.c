/*
 * ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psattribute.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psenv.h"

typedef struct {
    char *name;
    env_t scripts;
    env_t environment;
} attributes_t;

static attributes_t *attributes = NULL;
static uint8_t cnt = 0, size = 0;

char *Attr_name(const AttrIdx_t idx)
{
    if (idx < 0 || idx >= cnt) return NULL;
    return attributes[idx].name;
}

AttrIdx_t Attr_index(const char *name)
{
    if (!name) return -1;

    for (AttrIdx_t i = 0; i < cnt; i++)
	if (!strcmp(name, attributes[i].name)) return i;

    return -1;
}

AttrIdx_t Attr_add(const char *name)
{
    if (!name || Attr_index(name) != -1) return -1;

    if (cnt >= size) {
	size += 8;
	attributes_t *newAttr = realloc(attributes, size * sizeof(*attributes));

	if (!newAttr) {
	    size -= 8;
	    return -1;
	}
	attributes = newAttr;
    }

    attributes[cnt].name = strdup(name);
    attributes[cnt].scripts = envNew(NULL);
    attributes[cnt].environment = envNew(NULL);

    cnt++;
    return cnt-1;
}

AttrIdx_t Attr_num(void)
{
    return cnt;
}

bool HW_setScript(const AttrIdx_t idx, const char *type, const char *script)
{
    if (idx < 0 || idx >= cnt) return false;

    return envSet(&attributes[idx].scripts, type, script);
}

char *HW_getScript(const AttrIdx_t idx, const char *type)
{
    if (idx < 0 || idx >= cnt) return NULL;

    return envGet(&attributes[idx].scripts, type);
}

bool HW_setEnv(const AttrIdx_t idx, const char *name, const char *val)
{
    if (idx < 0 || idx >= cnt) return false;

    return envSet(&attributes[idx].environment, name, val);
}

char *HW_getEnv(const AttrIdx_t idx, const char *name)
{
    if (idx < 0 || idx >= cnt) return NULL;

    return envGet(&attributes[idx].environment, name);
}

int HW_getEnvSize(const AttrIdx_t idx)
{
    if (idx < 0 || idx >= cnt) return 0;

    return envSize(attributes[idx].environment);
}

char *HW_dumpEnv(const AttrIdx_t idx, const int num)
{
    if (idx < 0 || idx >= cnt) return NULL;

    return envDumpIndex(attributes[idx].environment, num);
}

char *Attr_print(AttrMask_t attrs)
{
    static char txt[256];

    txt[0] = '\0';

    if (!attrs) snprintf(txt, sizeof(txt), "none ");

    for (AttrIdx_t idx = 0; attrs; idx++, attrs >>= 1) {
	if (attrs & 1) {
	    char *name = Attr_name(idx);
	    if (!name) name = "<unknown>";
	    snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "%s ", name);
	}
    }

    txt[strlen(txt)-1] = '\0';

    return txt;
}
