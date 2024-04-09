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
#include <string.h>

#include "psenv.h"

char *testEnv[] = { "env1=bla", "env2=blub", "env3=test",
    "env4=test2", "env5=abc123", NULL, };

static bool cmpEnvArray(char **env, char **xpct)
{
    if (!env || !xpct) {
	fprintf(stderr, "parameter is NULL: env %p xpct %p\n", env, xpct);
	return false;
    }

    for (char **e = env, **x = xpct; *e || *x; e++, x++) {
	if (!*e || !*x) {
	    fprintf(stderr, "string is NULL: '%s' (expected '%s')\n", *e, *x);
	    return false;
	}
	if (strcmp(*e, *x)) {
	    fprintf(stderr, "strings differ: '%s' (expected '%s')\n", *e, *x);
	    return false;
	}
    }
    return true;
}

void dumpEnv(env_t env)
{
    int cnt = 0;
    for (char **e = envGetArray(env); e && *e; e++, cnt++)
	fprintf(stderr, "[%d]: '%s'\n", cnt, *e);
}

static bool filter(const char *envStr)
{
    if ((!strncmp(envStr, "env2", 4) && envStr[4] == '=')
	|| (!strncmp(envStr, "env4", 4) && envStr[4] == '=')
	|| (!strncmp(envStr, "env8", 4) && envStr[4] == '=')) return false;
    return true;
}

static bool evictFilter(const char *envStr, void *info)
{
    return !filter(envStr);
}

char *res1[] = { "env1=bla", "env5=abc123", "env3=test", "env4=test2", NULL, };
char *res2[] = { "env1=bla", "env3=test", "env5=abc123", NULL, };
char *res3[] = { "env1=bla", "env3=test", "env5=abc123", "env6=cdefg", NULL, };
char *res4[] = { "env1=bla", "env3=test", "env5=test", "env6=cdefg", NULL, };
char *res5[] = { "env1=bla", "env2=blub", "env3=test", "env4=test2",
    "env5=abc123", "env7=xyz987", NULL, };
char *res6[] = { "env1=bla", "env3=test", "env5=abc123", "env7=xyz987", NULL, };
char *res7[] = { "env1=bla", "env7=xyz987", "env3=test", "env5=abc123", NULL, };
char *res8[] = { "env1=bla", "env7=xyz987", "env3=test", "env5=abc123",
    "env6=cde123", NULL, };
char *res9[] = { "env1=bla", "env7=xyz987", "env3=test", "env5=abc123",
    "env6=cde789", NULL, };
char *res10[] = { "env1=bla", "env7=xyz987", "env3=tmp1", "env5=abc123",
    "env6=cde789", NULL, };
char *res11[] = { "env1=bla", "env7=xyz987", "env3=tmp2", "env5=abc123",
    "env6=cde789", NULL, };

static bool verbose = false;

int main(void)
{
    if (verbose) fprintf(stderr, "create\n");
    env_t env = envNew(testEnv);
    if (!cmpEnvArray(envGetArray(env), testEnv)) return -1;

    if (verbose) fprintf(stderr, "get\n");
    char *val = envGet(env, "env3");
    if (strcmp(val, "test")) {
	fprintf(stderr, "unexpected '%s' (expected 'test')\n", val);
	return -1;
    }


    if (verbose) fprintf(stderr, "clone 1\n");
    env_t clone = envClone(env, NULL);
    if (!cmpEnvArray(envGetArray(clone), testEnv)) return -1;
    envStealArray(env); // must no destroy or shred !

    if (verbose) fprintf(stderr, "unset\n");
    envUnset(clone, "env2");
    if (!cmpEnvArray(envGetArray(clone), res1)) return -1;
    envShred(clone);


    if (verbose) fprintf(stderr, "construct 1\n");
    env = envConstruct(testEnv, filter);
    if (!cmpEnvArray(envGetArray(env), res2)) return -1;

    if (verbose) fprintf(stderr, "set 1\n");
    envSet(env, "env6", "cdefg");
    if (!cmpEnvArray(envGetArray(env), res3)) return -1;

    if (verbose) fprintf(stderr, "set 2\n");
    envSet(env, "env5", "test");
    if (!cmpEnvArray(envGetArray(env), res4)) return -1;
    envDestroy(env);


    if (verbose) fprintf(stderr, "construct 2\n");
    env = envConstruct(testEnv, NULL);
    if (!cmpEnvArray(envGetArray(env), testEnv)) return -1;

    if (verbose) fprintf(stderr, "add\n");
    envAdd(env, "env7=xyz987");
    if (!cmpEnvArray(envGetArray(env), res5)) return -1;

    if (verbose) fprintf(stderr, "clone 2\n");
    clone = envClone(env, filter);
    if (!cmpEnvArray(envGetArray(clone), res6)) return -1;

    if (verbose) fprintf(stderr, "evict\n");
    envEvict(env, evictFilter, NULL);
    if (!cmpEnvArray(envGetArray(env), res7)) return -1;

    envDestroy(clone);


    if (verbose) fprintf(stderr, "put 1\n");
    char *envStr = strdup("env6=cde123");
    envPut(env, envStr);
    dumpEnv(env);
    if (!cmpEnvArray(envGetArray(env), res8)) return -1;

    if (verbose) fprintf(stderr, "tweak\n");
    envStr[8] = '7';
    envStr[9] = '8';
    envStr[10] = '9';
    dumpEnv(env);
    if (!cmpEnvArray(envGetArray(env), res9)) return -1;

    if (verbose) fprintf(stderr, "put 2\n");
    envStr = strdup("env3=tmp1");
    envPut(env, envStr);
    dumpEnv(env);
    if (!cmpEnvArray(envGetArray(env), res10)) return -1;

    if (verbose) fprintf(stderr, "tweak\n");
    envStr[8] = '2';
    dumpEnv(env);
    if (!cmpEnvArray(envGetArray(env), res11)) return -1;

    envDestroy(env);

    return 0;
}
