/*
 *               ParaStation3
 * parser.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: parser.h,v 1.4 2002/08/07 13:08:54 eicker Exp $
 *
 */
/**
 * @file
 * General parser utility for ParaStation daemon and admin
 *
 * $Id: parser.h,v 1.4 2002/08/07 13:08:54 eicker Exp $
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
typedef struct parser_T {
    char *delim;
    keylist_t *keylist;
} parser_t;

/**
 * @brief Initializes the parser module.
 *
 * Initializes the parser machinery for inputstream @a input.
 *
 *
 * @param usesyslog If true, all error-messages are printed via syslog().
 *
 * @param input The inputstream the parser is expected to act on. This
 * parameter is optional and may be NULL. If @a input is NULL, the
 * inputstream has to be set via @ref parser_setFile() before any
 * parsing is done.
 *
 *
 * @return No return value.
 *
 * @see parser_setFile()
 * */
void parser_init(int usesyslog, FILE *input);

/**
 * @brief Set the input stream.
 *
 * Set the inputstream to parse to @a input.
 *
 * @param input The inputstream the parser is expected to act on. *
 *
 * @return No return value.
 * */
void parser_setFile(FILE *input);

/**
 * @brief Parses a token.
 *
 * @todo Docu not up to date any more!!
 *
 * Parses the character array @a string pursuant to the syntax given
 * by @a parser.
 *
 *
 * @param string The character array to parse.
 *
 * @param parser The parser syntax used for parsing.
 *
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
 * keys, otherwise the return value of action() is returned.
 *
 * @return If @a string can be parsed without error (as shown by all
 * action() calls returning 0), 0 is returned. Otherwise the return
 * value of the first action() call displaying an error is returned.
 *
 * @see strtok(3)
 */
int parser_parseToken(char *token, parser_t *parser);

/**
 * @brief Parses a string.
 *
 * @todo Docu not up to date any more!!
 *
 * Parses the character array @a string pursuant to the syntax given
 * by @a parser.
 *
 *
 * @param string The character array to parse.
 *
 * @param parser The parser syntax used for parsing.
 *
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
 * keys, otherwise the return value of action() is returned.
 *
 * @return If @a string can be parsed without error (as shown by all
 * action() calls returning 0), 0 is returned. Otherwise the return
 * value of the first action() call displaying an error is returned.
 *
 * @see strtok(3)
 */
int parser_parseString(char *token, parser_t *parser);

/**
 * @brief Register a string to parse.
 *
 * @todo Docu not up to date!!
 *
 * Parses the character array @a string pursuant to the syntax given
 * by @a parser.
 *
 *
 * @param string The character array to parse.
 *
 * @param parser The parser syntax used for parsing.
 *
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
 * keys, otherwise the return value of action() is returned.
 *
 * @return If @a string can be parsed without error (as shown by all
 * action() calls returning 0), 0 is returned. Otherwise the return
 * value of the first action() call displaying an error is returned.
 *
 * @see strtok(3)
 */
char * parser_registerString(char *string, parser_t *parser);

/**
 * @brief Parses a character stream.
 *
 * Parses the character stream set via @ref parser_init() pursuant to
 * the syntax given by @parser.
 *
 *
 * @param parser The parser syntax used for parsing by passing to @ref
 * parser_parseString().
 *
 *
 * Parsing is done by reading whole lines from the character
 * stream. Following the line is parsed using @ref
 * parser_parseString().
 *
 * If @ref parser_parseString() returns 0, parsing goes on, otherwise
 * the return value of @ref parser_parseString() is returned.
 *
 * @return If the character stream can be parsed without error (as
 * shown by all parser_parseString() calls returning 0), 0 is
 * returned. Otherwise the return value of the first
 * parser_parseString() call displaying an error is returned.
 */
int parser_parseFile(parser_t *parser);

/**
 * @brief Quit parsing.
 *
 * Quit parsing. An error-message will be produced.
 *
 * @param token The actual token where the error was noticed.
 *
 * @return Always returns -1.
 *
 * @see keylist_t
 */
int parser_error(char *token);

/**
 * @brief Query the debug-level.
 *
 * Get the debug-level of the parser module.
 *
 * @return The actual debug-level is returned.
 *
 * @see parserr_setDebugLevel()
 */
int parser_getDebugLevel(void);

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
void parser_setDebugLevel(int level);

/**
 * @brief Print out a comment.
 *
 *
 * @param token The actual token where the error was noticed.
 *
 * @param comment A user-defined comment added to the message.
 *
 * @param level The @todo
 *
 *
 * @return No return value.
 */
void parser_comment(char *comment, int level);

/*
 * Basic routines to get defined fields
 */

/**
 * @brief Get another whitespace delemited string.
 *
 * Get another whitespace delemited string from the character array
 * passed to @ref parser_parseString() via strtok(NULL, " \t\n").
 *
 * @return The result of strtok(NULL, " \t\n") is returned.
 */
char *parser_getString(void);

/**
 * @brief Get the rest of the string to parse.
 *
 * Get the rest of the string to parse passed to @ref
 * parser_parseString() using strtok(NULL, "\n").
 *
 * @return The result of strtok(NULL, "\n") is returned.
 */
char *parser_getLine(void);

/**
 * @brief Get a comment during a running line.
 *
 * Get a comment during a running line. Actually, the rest of the line
 * is fetched via @ref parser_getLine() and thrown away.
 *
 * @param token The actual token where the comment was noticed.
 *
 * @return The return value of @ref parser_getLine() is passed thru.
 */
int parser_getComment(char *token);

/**
 * @brief Get a (positiv) number.
 *
 * Get a (positiv) number from the character array @a token.
 *
 * @param token The character array that contains the number.
 *
 * @return On success the number is returned, or -1 otherwise.
 */
long int parser_getNumber(char *token);

/**
 * @brief Get a filename.
 *
 * Get a filename (i.e. a whitespace delimited string) from the
 * character array @a token and test if the file exists. If the
 * filename is an absolut one, only the existence of filename itself
 * is tested. Otherwise first the existence of
 * prefix/extradir/filename and, on absence, the existence of
 * prefix/filename is tested.
 *
 *
 * @param token The character array that contains the filename.
 *
 * @param prefix The directory prefix to lookup the filename.
 *
 * @param extradir An optional directory to lookup the filename.
 *
 *
 * @return On success a pointer to the absolute filename is returned,
 * or NULL otherwise.
 */
char *parser_getFilename(char *token, char *prefix, char *extradir);

/**
 * @brief Get a hostname.
 *
 * Get a hostname (i.e. a whitespace delimited string) from the
 * character array @a token and test if it can be resolved.
 *
 * @param token The character array that contains the hostname.
 *
 * @return On success, the resolved IP address of the hostname is
 * returned, or 0 otherwise. On error, the @a h_errno variable holds
 * an error number.
 */
unsigned int parser_getHostname(char *token);

/**
 * @brief Get a (positiv) numerical value.
 *
 * Get a (positiv) numerical value from the character array @a token
 * via @ref parser_getNumber() and store it to @a *value. If an error
 * occurred (i.e. token contains no valid number), a message
 * concerning @a valname is produced and @a *value remains unchanged.
 *
 *
 * @param token The character array that contains the number.
 *
 * @param value Pointer to the value to get.
 *
 * @param valname The symbolic name of the value to get.
 *
 *
 * @return On success 0 is returned, or -1 otherwise. 
 */
int parser_getNumValue(char *token, int *value, char *valname);

/**
 * @brief Get a boolean value.
 *
 * Get a boolean value from the character array @a token and store it
 * to @a *value.  If an error occurred (i.e. token contains no valid
 * boolean value), a message concerning @a valname is produced and @a
 * *value remains unchanged.
 *
 *
 * @param token The character array that contains the boolean value.
 *
 * @param value Pointer to the value to get.
 *
 * @param valname The symbolic name of the value to get.
 *
 *
 * @return On success 0 is returned, or -1 otherwise.
 */
int parser_getBool(char *token, int *value, char *valname);

/**
 * @brief Continue to parse the file.
 *
 * Continue to parse the file. It is assumed that the current line was
 * started to parse. The rest of the line is passed as @a line and
 * parsed pursuant to the syntax given by @a parser using @ref
 * parser_parseString(). After the parsing of @a line is done, parsing
 * pursuant to @a parser continues using @ref parser_parseFile().
 *
 *
 * @param line The line parsing should start with.
 *
 * @param parser The parser syntax used for parsing.
 *
 *
 * @return If @ref parser_parseString() or @ref parser_parseFile()
 * return a value different from 0, this function returns immediately
 * with the given value. 0 is returned, if @ref parser_parseFile()
 * returns 0.
 */
int parser_parseOn(char *line, parser_t *parser);


#define UP 17 /* Some magic value */

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PARSING_H */
