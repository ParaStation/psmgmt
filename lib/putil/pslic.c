/***********************************************************
 *                  ParaStation4
 *
 *       Copyright (c) 2002 ParTec AG Karlsruhe
 *       All rights reserved.
 ***********************************************************/
/**
 * pslic.c: License handling
 *
 * $Id: pslic.c,v 1.2 2002/07/17 19:37:58 hauke Exp $
 *
 * @author
 *         Jens Hauke <hauke@par-tec.de>
 *
 * @file
 ***********************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <inttypes.h>
#include <paths.h>
#include <envz.h>

#include "pslic.h"
#include "psstrings.h"

char *lic_errstr = NULL;


int env_index(env_fields_t *env, const char *name)
{
    int len;
    int i;
    int idx = -1;

    if (!name || strchr(name,'=')) return -1; /* illegal name */
    len = strlen(name);
    for (i = 0; i < env->cnt; i++) {
	if ((strncmp(name, env->vars[i], len) == 0) && (env->vars[i][len] == '=')){
	    idx = i;
	    break;
	}
    }
    return idx;
}

int env_unset(env_fields_t *env, const char *name)
{
    int idx;

    idx = env_index(env, name);
    if (idx < 0) return -1;

    free(env->vars[idx]);
    env->cnt--;
    env->vars[idx] = env->vars[env->cnt]; /* cnt >= 1 because idx != -1 */
    env->vars[env->cnt] = NULL;
    
    return 0;
}

int env_set(env_fields_t *env, const char *name, const char *val)
{
    char *tmp;

    /* 
     * search for the name in string 
     */
    if (!name || strchr(name,'=')) return -1; /* illegal name */
    if (!val) val = "";

    env_unset(env, name);

    tmp = (char *)malloc(strlen(name) + 1 + strlen(val) + 1);
    tmp[0] = 0;
    strcpy(tmp, name);
    strcat(tmp, "=");
    strcat(tmp, val);

    if (env->size < env->cnt + 2) {
	env->size += 5;
	env->vars = (char **)realloc(env->vars, env->size * sizeof(char *));
    }
    env->vars[env->cnt] = tmp;
    env->cnt++;
    env->vars[env->cnt] = NULL;

    return 0;
}

char *env_get(env_fields_t *env, const char *name)
{
    int idx;

    idx = env_index(env, name);
    if (idx < 0) return 0;
    return strchr(env->vars[idx],'=') + 1;
}

void env_init(env_fields_t *env)
{
    memset(env, 0, sizeof(*env));
}

/*
  Read one line from file *f. return NULL on EOF or error.
  Empty lines (lines only with whitespaces) are ignored.
  Comment lines (lines which beginns with #) are ignored.
  heading and trailing whitespaces are removed.
  Continues lines are allowed ( \ at the end of the line).
*/
char *lic_readline(FILE *f, int *lineno)
{
    static char *line = NULL;
    const int part = 200;
    int len;
    int size;
    int ret;
    int lineend;
    int fileend;
    while (1) {
	len = 0;
	size = 0;

	do {
	    size += part;
	    line = (char *)realloc(line, size + 1);
	    line[len] = 0;
	    fileend = !fgets(&line[len], size - len, f);

	    len = strlen(line);

	    if ((len > 0) && (line[len-1] == '\n')){
		if (lineno) (*lineno)++;
		/* End of line */
		if ((len > 1) && (line[len-2] == '\\')){
		    /* continues line */
		    len -= 2;
		    line[len] = 0;
		    lineend = fileend;
		} else {
		    len--;
		    line[len] = 0;
		    lineend = 1;
		}
	    } else {
		/* more to read from line */
		lineend = fileend;
	    }
	} while (!lineend);

	strshrink(line);
	if (*line && (*line != '#')) {
	    return line;
	} else {
	    /* empty line or comment line */
	    if (fileend) {
		free(line);
		line = NULL;
		return NULL;
	    }
	}
    }
}



int lic_parseline(char *line, char **fieldname, char **val, char **rest)
{
    if (!*line){
	return -1;
    }

    /* Parse line: */
    *fieldname = strtok(line, "=");
    *val = strtok(NULL, "\0" );
    if (!*val || !*fieldname) return -1;

    *fieldname = strshrink(*fieldname);
    *val = strunquote_r(*val, rest);
    return 0;
}

int lic_fromfile(env_fields_t *env, char *filename)
{
    FILE *f;
    char *line;
    char **ret;
    int cnt;
    int lineno = 0;

    if (!filename) goto err_filename;
    
    if (strcmp("-", filename)){
	f = fopen(filename, "r");
    } else {
	f = stdin;
    }
    if (!f) goto err_fopen;

    while ((line = lic_readline(f, &lineno))){
	char *field, *val, *rest;
	if (!lic_parseline(line, &field, &val, &rest) && !rest){
	    env_set(env, field, val);
	} else goto err_parse;
    }

    if (f != stdin) fclose(f);
    return 0;
 err_filename:
    lic_errstr = (char*)realloc(lic_errstr, 100 + 1);
    snprintf(lic_errstr, 100, "No licensefile\n");
    return -1;
 err_fopen:
    lic_errstr = (char*)realloc(lic_errstr, 100 + 1);
    snprintf(lic_errstr, 100, "Cant open licensefile %s : %s\n", filename, strerror(errno));
    return -1;
 err_parse:
    if (f != stdin) fclose(f);
    lic_errstr = (char*)realloc(lic_errstr, 100 + 1);
    snprintf(lic_errstr, 100, "Error in licensefile %s:%d\n", filename, lineno);
    return -1;
}
