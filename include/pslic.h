/***********************************************************
 *                  ParaStation4
 *
 *       Copyright (c) 2002 ParTec AG Karlsruhe
 *       All rights reserved.
 ***********************************************************/
/**
 * pslic.h: Licensekey handling
 *
 * $Id: pslic.h,v 1.1 2002/07/16 19:25:13 hauke Exp $
 *
 * @author
 *         Jens Hauke <hauke@par-tec.de>
 *
 * @file
 ***********************************************************/

#ifndef _PSLIC_H_
#define _PSLIC_H_

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

#endif /* _PSLIC_H_ */
