/***********************************************************
 *                  ParaStation4
 *
 *       Copyright (c) 2002 ParTec AG Karlsruhe
 *       All rights reserved.
 ***********************************************************/
/**
 * env.c: Simple environment handling
 *
 * $Id: env.h,v 1.1 2003/03/24 17:51:37 eicker Exp $
 *
 * @author
 *         Jens Hauke <hauke@par-tec.de>
 *
 * @file
 ***********************************************************/

#ifndef _ENV_H_
#define _ENV_H_

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

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

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* _ENV_H_ */
