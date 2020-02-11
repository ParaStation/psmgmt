/*
 * ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "env.h"

#include "hardware.h"

typedef struct {
    char *name;
    env_fields_t scripts;
    env_fields_t environment;
} hardware_t;

static hardware_t *hw = NULL;
static int cnt = 0, size = 0;

char *HW_name(const int idx)
{
    if (idx < 0 || idx >= cnt) return NULL;
    return hw[idx].name;
}

int HW_index(const char *name)
{
    int i;

    if (!name) return -1;

    for (i=0; i<cnt; i++) {
	if (!strcmp(name, hw[i].name)) return i;
    }

    return -1;
}

int HW_add(const char *name)
{
    if (!name || HW_index(name) != -1) return -1;

    if (cnt >= size) {
	hardware_t *new_hw;
	size += 5;
	new_hw = realloc(hw, size * sizeof(*hw));

	if (!new_hw) {
	    size -= 5;
	    return -1;
	}
	hw = new_hw;
    }

    hw[cnt].name = strdup(name);
    env_init(&hw[cnt].scripts);
    env_init(&hw[cnt].environment);

    cnt++;

    return cnt-1;
}

int HW_num(void)
{
    return cnt;
}

int HW_setScript(const int idx, const char *type, const char *script)
{
    if (idx < 0 || idx >= cnt) return 0;

    return !env_set(&hw[idx].scripts, type, script);
}

char *HW_getScript(const int idx, const char *type)
{
    if (idx < 0 || idx >= cnt) return NULL;

    return env_get(&hw[idx].scripts, type);
}

int HW_setEnv(const int idx, const char *name, const char *val)
{
    if (idx < 0 || idx >= cnt) return 0;

    return !env_set(&hw[idx].environment, name, val);
}

char *HW_getEnv(const int idx, const char *name)
{
    if (idx < 0 || idx >= cnt) return NULL;

    return env_get(&hw[idx].environment, name);
}

int HW_getEnvSize(const int idx)
{
    if (idx < 0 || idx >= cnt) return 0;

    return env_size(&hw[idx].environment);
}

char *HW_dumpEnv(const int idx, const int num)
{
    if (idx < 0 || idx >= cnt) return NULL;

    return env_dump(&hw[idx].environment, num);
}

char *HW_printType(const unsigned int hwType)
{
    unsigned int hwT = hwType;
    int index = 0;
    static char txt[80];

    txt[0] = '\0';

    if (!hwT) snprintf(txt, sizeof(txt), "none ");

    while (hwT) {
	if (hwT & 1) {
	    char *name = HW_name(index);

	    if (name) {
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "%s ", name);
	    } else {
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "unknown ");
	    }
	}

	hwT >>= 1;
	index++;
    }

    txt[strlen(txt)-1] = '\0';

    return txt;
}
