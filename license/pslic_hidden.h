/***********************************************************
 *                  ParaStation4
 *
 *       Copyright (c) 2002 ParTec AG Karlsruhe
 *       All rights reserved.
 ***********************************************************/
/**
 * pslic_hidden.h: Licensekey handling
 *
 *
 *        DO NOT DISTRIBUTE THIS FILE !!!
 *
 *
 * $Id: pslic_hidden.h,v 1.1 2002/07/16 19:25:14 hauke Exp $
 *
 * @author
 *         Jens Hauke <hauke@par-tec.de>
 *
 ***********************************************************/

#ifndef _PSLIC_HIDDEN_H_
#define _PSLIC_HIDDEN_H_

#include <inttypes.h>
#include <time.h>

#include "pslic.h"
#include "psstrings.h"

/* Caculate the hash from all Fields in HashFields (, seppareted) */
extern inline char *lic_calchash(env_fields_t *env, char *HashFields)
{
    char *hf;
    char *fn;
    char *val;
    static char res[20];
    uint32_t h1 = *(uint32_t *)"halo";
    uint32_t h2 = *(uint32_t *)"blub";
    int cnt = 0;

    if (!HashFields) goto err;
    hf = strdup(HashFields);

    fn = strtok(hf, ", \t\n");
    
    while (fn) {
	val = env_get(env, fn);
	if (!val) goto err_envget;

	while (*val) {
	    h1 = h1 * (*val ^ cnt) + 42 + ((h2 & 0x105420) >> 2);
	    h2 = h2 - h1 * (*val ^ 42);
	    cnt++;
	    val++;
	}
	while (*fn) {
	    h2 = h2 - h1 * (*fn ^ 53);
	    fn++;
	}
	
	fn = strtok(NULL, ", \t\n");
    }

    snprintf(res, sizeof(res), "%08x-%08x", h1, h2);

    free(hf);
    return res;
 err_envget:
/*    printf("Cant Get value from field <%s>\n", fn);*/
    free(hf);
 err:
    return NULL;
}

/* check if the Licensefile is valid */
extern inline int lic_isvalid(env_fields_t *env)
{
    char *h = env_get(env, LIC_HASH);
    char *f = env_get(env, LIC_FIELDLIST);
    char *ch = lic_calchash(env, f ? f : "x");

    return !strcmp( h ? h : "", ch ? ch : "x");
}

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
    char *fl = fl ? strdup(_fl) : strdup("");
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

#endif /* _PSLIC_HIDDEN_H_ */
