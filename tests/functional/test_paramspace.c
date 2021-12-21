/*
 * ParaStation
 *
 * Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pscommon.h"
#include "psparamspace.h"

static char *test = NULL;

char * test_help(void *data)
{
    return strdup("This is some help message on 'test' which is in length"
		  " quite similar to the amazingly long help-message of"
		  " 'blub'");
}

static char *bla = NULL;
char * bla_help(void *data)
{
    return strdup("This is some help message on 'bla'");
}


static char *blub = NULL;
char * blub_set(void *data, char *value)
{
    char **strp = data;

    if (!data) return strdup("No place to store");

    free(*strp);
    *strp = strdup(value);
    if (value && ! *strp) return strdup("strdup() failed");

    return strdup("was set");
}

char * blub_help(void *data)
{
    return strdup("This is some really very very very very very very very very"
		  " very very very very very very very very very very very"
		  " very very very very very very very very very very very"
		  " very very very very very very very very very very very"
		  " very very very very very long help message on 'blub'");
}

static int int1=1, int2=17, int3=0;

int main(void)
{
    PSPARM_init();
    PSC_initLog(stderr);

    PSPARM_register("test", &test,
		    PSPARM_stringSet, PSPARM_stringPrint, test_help, NULL);
    PSPARM_register("bla", &bla,
		    PSPARM_stringSet, PSPARM_stringPrint, bla_help, NULL);
    PSPARM_register("blub", &blub, blub_set, PSPARM_stringPrint, blub_help, NULL);
    PSPARM_register("zero", NULL, NULL, NULL, NULL, NULL);
    PSPARM_register("zero", NULL, NULL, NULL, NULL, NULL);
    PSPARM_register("test2", NULL, NULL, NULL, NULL, NULL);
    PSPARM_register("dummy", NULL, NULL, NULL, NULL, NULL);
    PSPARM_register("some_parameter_with_an_long_name", NULL,
		    PSPARM_stringSet, PSPARM_stringPrint, test_help, NULL);
    PSPARM_register("int1", &int1, PSPARM_intSet, PSPARM_intPrint, NULL, NULL);
    PSPARM_register("int3", &int3, PSPARM_intSet, PSPARM_intPrint, NULL, NULL);
    PSPARM_register("int2", &int2, PSPARM_intSet, PSPARM_intPrint, NULL, NULL);
    PSPARM_register("int4", NULL, PSPARM_intSet, PSPARM_intPrint, NULL, NULL);

    printf("set blub\n");
    PSPARM_set("blub", "dummdidumm");

    printf("\nset test\n");
    PSPARM_set("test", "didummdidumm");

    printf("\nset zero\n");
    PSPARM_set("zero", "didummdidumm");

    printf("\nset some_parameter_with_an_long_name\n");
    PSPARM_set("some_parameter_with_an_long_name", "didummdidumm");

    printf("\nprint bla\n");
    PSPARM_print(NULL, "bla");
    printf("\nprint blub\n");
    PSPARM_print(NULL, "blub");
    printf("\nprint test\n");
    PSPARM_print(NULL, "test");

    printf("\nhelp blub\n");
    PSPARM_printHelp("blub");

    printf("\nhelp all\n");
    PSPARM_printHelp(NULL);

    printf("\nprint all\n");
    PSPARM_print(NULL, "");

    printf("\nSet int1\n");
    PSPARM_set("int1", "21");

    printf("\nSet int3\n");
    PSPARM_set("int3", "bla");

    printf("\nSet int4\n");
    PSPARM_set("int4", "21");
    
    printf("\nRemove blub\n");
    PSPARM_remove("blub");

    printf("\nClear bla\n");
    PSPARM_set("bla", NULL);
    
    printf("\nprint all\n");
    PSPARM_print(NULL, "");

    printf("\nFinalize\n");
    PSPARM_finalize();

    printf("\nprint all\n");
    PSPARM_print(NULL, "");

    return 0;
}
