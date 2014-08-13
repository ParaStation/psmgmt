/*
 * ParaStation
 *
 * Copyright (C) 2002-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "psstrings.h"

/* ps_isspace: Like isspace() in the standard `"C"' locale.
 * This function dont need GLIBC 2.3 __ctype_b_loc */
static
int ps_isspace(unsigned char ch)
{
    switch (ch) {
    case ' ':  /* space */
    case '\f': /* formfeed */
    case '\n': /* newline */
    case '\r': /* carriage */
    case '\t': /* horizontal tab */
    case '\v': /* vertical tab */
	return 1;
    default:
	return 0;
    }
}

/* remove head and tail spaces. return the modified str (equal str) */
char *strshrink(char *str)
{
    char *beg = NULL;
    int len = 0;
    char *tmp;

    if (!str) return str;
    
    for (tmp = str; *tmp; tmp++){
	if (!ps_isspace(*tmp)) {
	    if (!beg) {
		beg = tmp;
		len = 1;
	    } else {
		len = 1 + tmp - beg;
	    }
	}
    };
    if (beg){
	memmove(str, beg, len);
    }
    str[len] = 0;
    return str;
}

/* Unquote string (enclosed in ""). Quote single " with \.
   return unquoted the string (equal str). *ptrptr is set
   to the rest (if any e.g. <"abc"xyz> set ptrptr to <xyz>).
   ToDo: Unterminated strings are not detected! */
char *strunquote_r(char *str, char **ptrptr)
{
    char *s, *d;
    char *rest = NULL;
    
    strshrink(str);
    if (!str) goto out;

    if (str[0] != '"') {
	/* String is not quoted */
	str = strtok_r(str, _SPACES, &rest);
	goto out;
    }

    d = str;
    s = str + 1;
    
    while (*s && (*s != '"')) {
	if (*s != '\\') {
	    *d = *s;
	} else {
	    s++;
	    *d = *s;
	}
	s++; d++;
    }
    if (*s == '"') {
	*d = '\0';
	rest = s + 1;
    } else {
	/* ToDo: Here is the unterminated string ! */
    }
 out:
    if (ptrptr) *ptrptr = rest ? (rest[0] ? rest : NULL) : NULL;
    return str;
}

/* Translate a ISO 8601 Date (YYYY-MM-DD) to senconds since 1970,
 *  return <default> on parse error.
 */
long int str_datetotime_d(char *str, long int def)
{
    struct tm tm;
    char *tmp, *work;
    char *d = NULL;
    long int ret;

    if (!str) goto err;
    
    memset(&tm, 0, sizeof(tm));
    d = strdup(str);

    tmp = strtok_r(d, "-", &work);
    if (!tmp) goto err;
    tm.tm_year = strtol(tmp, NULL, 10) - 1900;

    tmp = strtok_r(NULL, "-", &work);
    if (!tmp) goto err;
    tm.tm_mon = strtol(tmp, NULL, 10) - 1;

    tmp = strtok_r(NULL, "", &work);
    if (!tmp) goto err;
    tm.tm_mday = strtol(tmp, NULL, 10);
    
    ret = mktime(&tm);
    if (ret < 0) goto err;

    if (d) free(d);
    return ret;
 err:
    if (d) free(d);
    return def;
}

