/*
 * Copyright (c) 2001 ParTec AG
 * All rights reserved.
 *
 */
#ifndef __PSIENV_H
#define __PSIENV_H

extern char** PSI_environ;     /* Environment for spawned processes */
extern int    PSI_environc;

/*----------------------------------------------------------------------*/
/* 
 * PSI_clearenv()
 *  
 *  The PSI_clearenv() function clears the process PSI_ environment.
 *  No PSI_environment variables are defined immediately after a call
 *  to PSI_clearenv().  
 *  
 * PARAMETERS
 * RETURN  0 on success
*          -1 on error
 */
int PSI_clearenv(void);

/*----------------------------------------------------------------------*/
/* 
 * PSI_putenv(char *string)
 *  
 *  Puts the environment variable to the environemnt, which child processes
 *  will get.
 *  
 * PARAMETERS
 *  string    Points to a name=value string.
 * RETURN  0 on success
 *        -1 on error
 */
int PSI_putenv(char *string);

/*----------------------------------------------------------------------*/
/* 
 * PSI_getenv(char *name)
 *  
 *  The PSI_getenv() function searches the environment list for a string
 *  of the form name=value, and returns a pointer to a string containing
 *  the corresponding value for name.
 *  
 * PARAMETERS
 *      name      Specifies the name of an environment variable.
 * RETURN  0 on success
 *        -1 on error
 */
char* PSI_getenv(char *name);

#endif 
