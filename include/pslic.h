/***********************************************************
 *                  ParaStation4
 *
 *       Copyright (c) 2002 ParTec AG Karlsruhe
 *       All rights reserved.
 ***********************************************************/
/**
 * pslic.h: Licensekey handling
 *
 * $Id: pslic.h,v 1.5 2002/08/23 17:47:16 hauke Exp $
 *
 * @author
 *         Jens Hauke <hauke@par-tec.de>
 *
 * @file
 ***********************************************************/

#ifndef _PSLIC_H_
#define _PSLIC_H_

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "psstrings.h"

typedef struct env_fields_s{
    char **vars;
    int cnt;
    int size;
}env_fields_t;

int env_index(env_fields_t *env, const char *name);
int env_unset(env_fields_t *env, const char *name);
int env_set(env_fields_t *env, const char *name, const char *val);
char *env_get(env_fields_t *env, const char *name);
void env_init(env_fields_t *env);


char *lic_readline(FILE *f, int *lineno);
int lic_parseline(char *line, char **fieldname, char **val, char **rest);
int lic_fromfile(env_fields_t *env, char *filename);

/* Error message of last error */
extern char *lic_errstr;

/* Common fields */
#define LIC_FIELDLIST	"Fields"
#define LIC_HASH	"Hash"
#define LIC_DATE	"Date"
#define LIC_EXPIRE	"Expire"

/* ParaStation fields */
#define LIC_FEATURES    "Features"
#define LIC_FEATURE_IP   "IP"
#define LIC_FEATURE_ETH  "Ethernet"
#define LIC_FEATURE_Myri "Myrinet"

#define LIC_MCPKEY	"MCPKey"
#define LIC_NODES	"Nodes"
#define LIC_CPUs	"CPUs"

#define LIC_LICENSE	"License"



/* check if the License is expired */
extern inline int lic_isexpired(env_fields_t *env)
{
    long int from, to, now;

    now = time(NULL);
    from = str_datetotime_d(env_get(env, LIC_DATE), 0);
    to = str_datetotime_d(env_get(env,LIC_EXPIRE), now + 1);

    return (now < from) || (to < now);
}

/* check for a feature inside a featurelist (case sensitive!) */
extern inline int lic_hasfeature(env_fields_t *env, char *featurevar, char *feature)
{
    int ret = 0;
    char *_fl = env_get(env, featurevar);
    char *fl = _fl ? strdup(_fl) : strdup("");
    char *f;
    
    f = strtok(fl, " \t\n");
    while (f) {
	if (!strcmp(f, feature)){
	    ret = 1;
	    break;
	}
	f = strtok(NULL, " \t\n");
    }
    free(fl);
    return ret;
}

/* get a nummerical value. return def on error */
extern inline int lic_numval(env_fields_t *env, char *varname, int def)
{
    char *val = env_get(env, varname);
    char *err;
    int ret;
    ret = strtol(val ? val : "x", &err, 10);
    return (*err) ? def : ret;
}

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* _PSLIC_H_ */
