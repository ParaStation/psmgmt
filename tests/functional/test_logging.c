/*
 * ParaStation
 *
 * Copyright (C) 2005-2011 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "logging.h"

void my_print(logger_t log, long key, char *format, ...)
{
    static char *fmt = NULL;
    static int fmtlen = 0;
    va_list ap;
    int len;

    len = snprintf(fmt, fmtlen, "mine: %s\n", format);
    if (len >= fmtlen) {
	fmtlen = len + 80; /* Some extra space */
	fmt = (char *)realloc(fmt, fmtlen);
	sprintf(fmt, "mine: %s\n", format);
    }

    va_start(ap, format);
    logger_vprint(log, key, fmt, ap);
    va_end(ap);
}


void log_it(logger_t my)
{
    int count = 1, i;

    logger_print(my ,-1, "\n");
    logger_print(my ,-1, "test%d '%s'\n", count++, "bla");
    logger_print(my ,-1, "test%d %d\n", count++, 17);
    logger_print(my ,-1, "test%d '%s' %d\n", count++, "bla", 17);
    logger_print(my ,-1, "test%d '%s'", count++, "bla");
    logger_print(my ,-1, " %d\n", 17);
    my_print(my ,-1, "test%d '%s'", count++, "bla");

    logger_print(my , 6, "test%d '%s'\n", count++, "bla");
    logger_print(my , 6, "test%d %d\n", count++, 17);
    my_print(my , 6, "test%d '%s'", count++, "bla");

    logger_print(my , 4, "test%d '%s' %d\n", count++, "bla", 17);
    logger_print(my , 4, "test%d '%s'", count++, "bla");
    for (i=0; i<10; i++) {
	logger_print(my ,4, " # test%d '%s'", count++, "bla");
    }
    logger_print(my , 4, " %d\n", 17);
    my_print(my , 4, "test%d '%s'", count++, "bla");

    logger_warn(my, -1, 0, "test%d", count++);
    logger_warn(my, -1, 3, "test%d", count++);

    logger_print(my ,-1, "test%d '%s'", count++, "bla");
}

int main(void)
{
    logger_t my = logger_new("TEST", stderr);
    if (!my) {
	fprintf(stderr, "Failed to initialize logger 'my'\n");
	exit(1);
    }
    logger_t my2 = logger_new("TEST2", stderr);
    if (!my2) {
	fprintf(stderr, "Failed to initialize logger 'my2'\n");
	logger_finalize(my);
	exit(1);
    }

    log_it(my);
    log_it(my2);
    log_it(my);

    logger_print(my, -1, "\n");
    logger_setMask(my, 2);
    log_it(my);

    logger_print(my, -1, "\n");
    logger_setMask(my, 4);
    log_it(my);

    logger_print(my, -1, "\n");
    logger_exit(my, 0, "final %s", "bla");

    logger_finalize(my2);
    logger_finalize(my);

    return 0;
}
