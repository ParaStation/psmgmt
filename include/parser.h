/*
 *               ParaStation3
 * parsing.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: parser.h,v 1.1 2002/05/02 13:21:35 eicker Exp $
 *
 */
/**
 * @file
 * General parser utility for ParaStation daemon and admin
 *
 * $Id: parser.h,v 1.1 2002/05/02 13:21:35 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PARSING_H
#define __PARSING_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

#include <stdio.h>

/**
 * Information container for parser calls.
 * If the token @a key is found @action will be called with argument @a key.
 */
typedef struct keylist_T {
    char *key;
    int (*action)(char*);

} keylist_t;

/**
 * Information container for parser calls.
 */
typedef struct parse_T {
    char *delim;
    keylist_t *keylist;
} parse_t;

/**
 * @brief Initializes the parser module.
 *
 * Initializes the parser machinery for inputstream @a input.
 *
 * @param usesyslog If true, all error-messages are printed via syslog().
 * @param input The inputstream the parser is expected to act on.
 */
void parse_init(int usesyslog, FILE *input);

/**
 * @brief Parses a string.
 *
 * Parses the character array @a string pursuant to the syntax given
 * by @a parser.
 *
 * @param string The character array to parse.
 * @param parser The parser syntax used for parsing.
 *
 * Parsing is started by getting the first token in @a string via
 * strtok(). Tokens are delimited per @a parser->delim. Tokens will be
 * converted to lowercase characters. The converted token is compared
 * to each key in @a parser->keylist. If a key matches the token, the
 * corresponding action() is called. The token is passed as an
 * argument.
 *
 * If the last key in @a parser->keylist also does not match like all
 * keys befor @b and is NULL, the corresponding action() is called
 * with token passed as an argument (default action).
 *
 * If action() returns 0, parsing goes on without regard to further
 * keys, otherwise the return value of action() is returned by
 * parse_string().
 *
 * @return If @a string can be parsed without error (as shown by all
 * action() calls returning 0), 0 is returned. Otherwise the return
 * value of the first action() call displaying an error is returned.
 *
 * @see strtok(3)
 */
int parse_string(char *string, parse_t *parser);

/**
 * @brief Parses a character stream.
 *
 * Parses the character stream set via @ref parse_init() pursuant to
 * the syntax given by @parser.
 *
 * @param parser The parser syntax used for parsing by passing to
 * @ref parse_string().
 *
 * Parsing is done by reading whole lines from the character
 * stream. Following the line is parsed using @ref parse_string().
 *
 * If @ref parse_string() returns 0, parsing goes on, otherwise the
 * return value of @ref parse_string() is returned by parse_file().
 *
 * @return If the character stream can be parsed without error (as
 * shown by all parse_string() calls returning 0), 0 is
 * returned. Otherwise the return value of the first parse_string()
 * call displaying an error is returned.
 * */
int parse_file(parse_t *parser);

/**
 * @brief Query the debug-level.
 *
 * Get the debug-level of the parser module.
 *
 * @return The actual debug-level is returned.
 *
 * @see parser_setDebugLevel()
 */
int parse_getDebugLevel(void);

/**
 * @brief Set the debug-level.
 *
 * Set the debug-level of the parser module. Possible values are:
 *  - 0: Critical errors (usually exit). This is the default.
 *  - 5: Critical errors (usually exit). This is the default.
 *
 * @param level The debug-level to set.
 *
 * @return No return value.
 *
 * @see parser_getDebugLevel()
 */
void parse_setDebugLevel(int level);

/*
 * Basic routines to get defined fields
 */

/**
 * @brief Get another whitespace delemited string.
 *
 * Get another whitespace delemited string from the character array
 * passed to @ref parse_string() via strtok(NULL, " \t\n").
 *
 * @return The result of strtok(NULL, " \t\n") is returned.
 */
char *parse_getString(void);

/**
 * @brief Get the rest of the string to parse.
 *
 * Get the rest of the string to parse passed to @ref parse_string()
 * using strtok(NULL, "\n").
 *
 * @return The result of strtok(NULL, "\n") is returned.
 */
char *parse_getLine(void);

/**
 * @brief Get a (positiv) number.
 *
 * Get a (positiv) number from the character array token.
 *
 * @param token The character array that contains the number.
 *
 * @return On success the number is returned, or -1 otherwise.
 */
long int parse_getNumber(char *token);

/**
 * @brief Get a filename.
 *
 * Get a filename (i.e. a whitespace delimited string) from the
 * character array passed to @ref parse_string() and test if the file
 * exists. If the filename is an absolut one, only the existence of
 * filename itself is tested. Otherwise first the existence of
 * prefix/extradir/filename and if this does not exists, the existence
 * of prefix/filename is tested.
 *
 * @param prefix The directory prefix to lookup the filename.
 * @param extradir An optional directory to lookup the filename.
 *
 * @return On success a pointer to the absolute filename is returned,
 * or NULL otherwise.
 */
char *parse_getFilename(char *prefix, char *extradir);

/**
 * @brief Get a hostname.
 *
 * Get a hostname (i.e. a whitespace delimited string) from the
 * character array passed to @ref parse_string() and test if it can be
 * resolved.
 *
 * @return On success a pointer to the hostname is returned, or NULL
 * otherwise.
 */
char *parse_getHostname(void);

/**
 * @brief Get a (positiv) numerical value.
 *
 * Get a (positiv) numerical value from the character array @a token
 * via @ref parse_getNumber() and store it to @a *value. If an error
 * occurred, a message concerning @a valname is returned.
 *
 * @param token The character array that contains the number.
 * @param value Pointer to the value to get.
 * @param valname The symbolic name of the value to get.
 *
 * @return On success 0 is returned, or -1 otherwise. 
 */
int parse_getNumValue(char *token, int *value, char *valname);

/**
 * @brief Continue to parse the file.
 *
 * Continue to parse the file. It is assumed that the current line was
 * started to parse. The rest of the line is passed as @a line and
 * parsed pursuant to the syntax given by @a parser using @ref
 * parse_string(). After the parsing of @a line is done, parsing
 * pursuant to @a parser continues using @ref parse_file().
 *
 * @param line The line parsing should start with.
 * @param parser The parser syntax used for parsing.
 *
 * @return If @ref parse_string() or @ref parse_file() return a value
 * different from 0, parse_on() returns immediately with the given
 * value. 0 is returned, if @ref parse_file() returns 0.
 */
int parse_continue(char *line, parse_t *parser);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PARSING_H */
