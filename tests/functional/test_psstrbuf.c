/*
 * ParaStation
 *
 * Copyright (C) 2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psstrbuf.h"

#define MALLOC_GRANULARITY 64

static bool checkStr(strbuf_t strbuf)
{
    if (!strbufInitialized(strbuf)) {
	fprintf(stderr, "strbuf %p not initialzied\n", strbuf);
	return false;
    }
    if (strbufSize(strbuf) % MALLOC_GRANULARITY) {
	fprintf(stderr, "strbuf %p: unexpected size %u\n", strbuf,
		strbufSize(strbuf));
	return false;
    }
    if (strbufLen(strbuf) != strlen(strbufStr(strbuf)) + 1) {
	fprintf(stderr, "strbuf %p: unexpected length %u (expected %zu)\n",
		strbuf, strbufLen(strbuf), strlen(strbufStr(strbuf)) + 1);
	return false;
    }
    return true;
}

static bool cmpStr(char *str, const char *xpct)
{
    if (!str || !xpct) {
	fprintf(stderr, "parameter is NULL: str %p xpct %p\n", str, xpct);
	return false;
    }

    if (strcmp(str, xpct)) {
	fprintf(stderr, "strings differ: '%s' (expected '%s')\n", str, xpct);
	return false;
    }
    return true;
}

static bool createCheck(const char *testStr)
{
    strbuf_t str = strbufNew(testStr);
    if (!checkStr(str)) return false;
    if (!cmpStr(strbufStr(str), testStr)) return false;
    strbufDestroy(str);

    return true;
}

char testStr[] = "testStr\'\"\ntail";

#define str10 "1234567890"
#define str20 str10 str10
#define str40 str20 str20

#define str62 str40 str20 "12"
#define str63 str40 str20 "123"
#define str64 str40 str20 "1234"

//static bool verbose = false;
static bool verbose = true;

int main(void)
{
    if (verbose) fprintf(stderr, "create\n");
    createCheck(testStr);

    if (verbose) fprintf(stderr, "create corner case 1 (size 62)\n");
    createCheck(str62);

    if (verbose) fprintf(stderr, "create corner case 2 (size 63)\n");
    createCheck(str63);

    if (verbose) fprintf(stderr, "create corner case 3 (size 64)\n");
    createCheck(str64);

    if (verbose) fprintf(stderr, "add 1\n");
    strbuf_t str = strbufNew(str40);
    strbufAdd(str, str62);
    if (!checkStr(str)) return -1;
    if (!cmpStr(strbufStr(str), str40 str62)) return -1;

    if (verbose) fprintf(stderr, "add 2\n");
    strbufAdd(str, str20);
    if (!checkStr(str)) return -1;
    if (!cmpStr(strbufStr(str), str40 str62 str20)) return -1;

    if (verbose) fprintf(stderr, "destroy\n");
    char *contentStr = strbufStr(str);
    if (!cmpStr(contentStr, str40 str62 str20)) return -1;
    strbufDestroy(str);
    if (strbufInitialized(str)) {
	fprintf(stderr, "strbuf %p: still valid?!\n", str);
	return -1;
    }
    if (cmpStr(contentStr, str40 str62 str20)) {
	fprintf(stderr, "untouched content '%s'?!\n", contentStr);
	return -1;
    }

    if (verbose) fprintf(stderr, "steal 1\n");
    // preparation
    str = strbufNew(str40);
    strbufAdd(str, str62);
    if (!checkStr(str)) return -1;
    if (!cmpStr(strbufStr(str), str40 str62)) return -1;
    contentStr = strbufStr(str);
    if (!cmpStr(contentStr, str40 str62)) return -1;
    if (!checkStr(str)) return -1;
    // check
    strbufSteal(str);
    if (strbufInitialized(str)) {
	fprintf(stderr, "strbuf %p: still valid?!\n", str);
	return -1;
    }
    if (!cmpStr(contentStr, str40 str62)) return -1;
    free(contentStr);

    if (verbose) fprintf(stderr, "steal 2\n");
    // preparation
    str = strbufNew(str40);
    strbufAdd(str, str62);
    if (!checkStr(str)) return -1;
    if (!cmpStr(strbufStr(str), str40 str62)) return -1;
    // check
    contentStr = strbufSteal(str);
    if (!cmpStr(contentStr, str40 str62)) return -1;
    if (strbufInitialized(str)) {
	fprintf(stderr, "strbuf %p: still valid?!\n", str);
	return -1;
    }
    free(contentStr);

    return 0;
}
