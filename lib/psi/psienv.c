/*
 *
 *      @(#)psi.c    1.00 (Karlsruhe) 03/11/97
 *
 *      written by Joachim Blum
 *
 *
 *
 *  History
 *
 *   991227 Joe changed to new Daemon-Daemon protocol
 *   970311 Joe Creation: used psp.h and changed some things
 */
#include <stdlib.h>
#include <string.h>

#include "psienv.h"

char** PSI_environ  = NULL;
int    PSI_environc = 0;

/*----------------------------------------------------------------------*/
/* 
 * PSI_clearenv()
 *  
 *  The PSI_clearenv() function clears the process PSI_environment.
 *  No PSI_environment variables are defined immediately after a call
 *  to clearenv().  
 *  
 * PARAMETERS
 * RETURN  0 on success
*          -1 on error
 */
int PSI_clearenv()
{
    int i;
    for(i=0;i<PSI_environc;i++)
	if(PSI_environ[i])
	    free(PSI_environ[i]);
    if(PSI_environ) free(PSI_environ);
    PSI_environ=NULL;
    PSI_environc=0;

    return 0;
}

/*----------------------------------------------------------------------*/
/*
 * PSI_putenv()
 *
 *  puts the environment variable to the environemnt, which child processes
 *  will get.
 *
 * PARAMETERS
 *  string    Points to a name=value string.
 * RETURN  0 on success
 *        -1 on error
 */
int PSI_putenv(char* string)
{
    char* beg;
    int len;
    int i;
    /* 
     * search for the name in string 
     */
    beg = strchr(string,'=');
    if(beg==NULL)
	return -1;
    len = ((long)beg) - ((long)string);

    for(i=0;i<PSI_environc;i++)
	if((PSI_environ[i]) && (strncmp(PSI_environ[i],string,len+1)==0))
	    /* the environment strings are the same, including the "=" */
	    break;
    if(i<PSI_environc){
	/* the name is found => replace it */
	free(PSI_environ[i]);
	PSI_environ[i] = strdup(string);
    }else{
	for(i=0;i<PSI_environc && PSI_environ[i];i++);
	if(i<PSI_environc){
	    /* a free space is found */
	    PSI_environ[i] = strdup(string);
	}else{
	    /* extend the environment */
	    char** new_environ;
	    new_environ = malloc(sizeof(char*)*(PSI_environc+5));
	    if(new_environ== NULL) return -1;
	    for(i=0;i<PSI_environc;i++)
		new_environ[i] = PSI_environ[i];
	    new_environ[PSI_environc] = strdup(string);
	    for(i=PSI_environc+1;i<PSI_environc+5;i++)
		new_environ[i]= NULL;
	    PSI_environc += 5;
	    if(PSI_environ)
		free(PSI_environ);
	    PSI_environ = new_environ;
	}
    }
    return 0;
}

/*----------------------------------------------------------------------*/
/* 
 * PSI_getenv()
 *  
 *  The PSI_getenv() function searches the environment list for a string of
 *  the form name=value, and returns a pointer to a string containing the
 *  corresponding value for name.
 *  
 * PARAMETERS
 *      name      Specifies the name of an environment variable.
 * RETURN  0 on success
*          -1 on error
 */
char* PSI_getenv(char* name)
{
    int len;
    int i;
    /* 
     * search for the name in string 
     */
    if(name==NULL)
	return NULL;
    len = strlen(name);

    for(i=0;i<PSI_environc;i++)
	if((PSI_environ[i]) && (strncmp(PSI_environ[i],name,len)==0))
	    break;
    if(i<PSI_environc)
	return &(PSI_environ[i])[len+1];
    else
	return NULL;
}
