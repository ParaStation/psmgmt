

#ifndef _COMPAT_TRU64_H_
#define _COMPAT_TRU64_H_


#define __attribute__( name )

#define _DEC_XPG
/*typedef unsigned long   socklen_t;*/

/* #define _BSD */
/* Only defined if _BSD is defined */
extern int      setenv   (const char *, const char *, int);
extern void     unsetenv (const char *);

#define NO_MACRODOTDOT



#endif /* _COMPAT_TRU64_H_ */



