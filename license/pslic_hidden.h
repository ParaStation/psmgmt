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
 * $Id: pslic_hidden.h,v 1.2 2002/07/17 19:37:58 hauke Exp $
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

#endif /* _PSLIC_HIDDEN_H_ */
