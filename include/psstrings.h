/***********************************************************
 *                  ParaStation4
 *
 *       Copyright (c) 2002 ParTec AG Karlsruhe
 *       All rights reserved.
 ***********************************************************/
/**
 * psstrings.h: string handling
 *
 * $Id: psstrings.h,v 1.1 2002/07/16 19:25:13 hauke Exp $
 *
 * @author
 *         Jens Hauke <hauke@par-tec.de>
 *
 * @file
 ***********************************************************/

#ifndef _PSSTRINGS_H_
#define _PSSTRINGS_H_


#define _SPACES "\t "

/**
 * @brief Remove head and tail spaces
 * Remove head and tail spaces. These function modify the str argument
 *
 * @param str 
 *
 * @return Same as str
 */
char *strshrink(char *str);

/* Unquote string (enclosed in ""). Quote single " with \.
   return unquoted the string (equal str). *ptrptr is set
   to the rest (if any e.g. <"abc"xyz> set ptrptr to <xyz>).
   ToDo: Unterminated strings are not detected! */

/**
 * @brief Unquote a string
 *
 * Unquote string (enclosed in ""). Quote single " inside with \.
 *  
 * @param str
 * @param ptrptr if ptrptr !NULL, *ptrptr is set to the rest of the string
 * (if any e.g. <"abc"xyz> set *ptrptr to <xyz>)
 *
 * @return Same as str
 */
char *strunquote_r(char *str, char **ptrptr);

/**
 * @brief Translate a ISO 8601 Date (YYYY-MM-DD) to senconds since 1970
 *
 *  
 * @param str the date as a string
 * @param def default value
 *
 * @return Seconds since 1970, or def on parse error.
 */
long int str_datetotime_d(char *str, long int def);

#endif /* define _PSSTRINGS_H_ */
