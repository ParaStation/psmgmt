/*
 * ParaStation
 *
 * Copyright (C) 2002-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * General parser utility for ParaStation daemon and admin
 */
#ifndef __PARSING_H
#define __PARSING_H

#include <stdint.h>
#include <stdio.h>
#include <netinet/in.h>

#include "logging.h"

/**
 * Information container for parser calls.
 * If the token @a key is found @action will be called with argument @a key.
 */
typedef struct keylist_T {
    char *key;                /**< The key to be matched by a given token */
    int (*action)(char*);     /**< Action to trigger by a matching token */
    struct keylist_T *next;   /**< Keylist to be used on the next
			       * token. This will be used in order to
			       * setup correct autocompletion */
} keylist_t;

/** Information container for parser calls. */
typedef struct {
    char* delim;              /**< Delimiters used to split into tokens*/
    keylist_t* keylist;       /**< Keylist to match the given token */
} parser_t;

/**
 * @brief Initializes the parser module.
 *
 * Initializes the parser machinery for inputstream @a input.
 *
 *
 * @param logfile File to use for logging. If NULL, syslog(3) is used.
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
 */
void parser_init(FILE* logfile, FILE* input);

/**
 * @brief Set the input stream.
 *
 * Set the inputstream to parse to @a input.
 *
 * @param input The inputstream the parser is expected to act on. *
 *
 * @return No return value.
 * */
void parser_setFile(FILE* input);

/**
 * @brief Handle a token.
 *
 * Handle the character array @a token pursuant to the syntax given by
 * @a parser.
 *
 * In order to handle it, @a token will be converted to lowercase
 * characters. The converted token is compared to each key in @a
 * parser->keylist. If a key matches the token, the corresponding
 * action() is called. The token is passed as an argument.
 *
 * If the last key in @a parser->keylist also does not match like all
 * keys before @b and is NULL, the corresponding action() is called
 * with token passed as an argument (default action).
 *
 * @param token The character array to handle.
 *
 * @param parser The parser syntax used for parsing.
 *
 * @return If @a token matches a key, the return value of the
 * corresponding action is returned. Otherwise 0 is returned.
 */
int parser_parseToken(char* token, parser_t* parser);

/**
 * @brief Get the next keylist
 *
 * According to a given @a keylist a match of the token @a token might
 * define additional keylist used in order to auto-complete some given line.
 *
 * The keylist to use for future auto-completion is defined within the
 * @a next member of the entry in the @keylist which matches the
 * given @a token. Upon return @a matched will point to the
 * corresponding key uniquely matching the @a token under investigation.
 *
 * @param token The token to search in the keylist
 *
 * @param keylist The keylist to match the given token
 *
 * @param matched Upon return this points to the matched key, if found any
 *
 * @return Return the next keylist as defined by the matched token.
 */
keylist_t * parser_nextKeylist(char *token, keylist_t *keylist, char **matched);

/**
 * @brief Register a string to parse.
 *
 * Register the character array @a string to get parsed pursuant to
 * the syntax given by @a parser. The actual parsing is done within
 * subsequent calls to @ref parser_parseString().
 *
 * Parsing is started by getting the first token in @a string via
 * strtok_r() while using @a parser->delim as delimiters. The token
 * gained in this way is returned to the calling function and usually
 * will be handled by further call to @ref parser_parseString().
 *
 * @param string The character array to parse.
 *
 * @param parser The parser syntax used for parsing.
 *
 * @return The first token returned by the registering strtok_r() call
 * is returned.
 *
 * @see strtok_r(3)
 */
char* parser_registerString(char* string, parser_t* parser);

/**
 * @brief Parses a string.
 *
 * Parses the string registered via @ref parser_registerString()
 * pursuant to the syntax given by @a parser.
 *
 * Parsing is started by registering a string using @ref
 * parser_registerString(). The token returned by this function has to
 * be passed to this function as the @a token argument. Further tokens
 * will be gained within this function using strtok_r() subsequently.
 *
 * Each token will be handled using the @ref parser_parseToken()
 * function. The return value of this function will steer the ongoing
 * parsing process. As long as 0 is returned, parsing goes on by
 * getting the next token and evaluating it using @ref
 * parser_parseToken(). Otherwise the parsing will be interrupted
 * returning the corresponding value.
 *
 * @param token The first token to handle.
 *
 * @param parser The parser syntax used for parsing.
 *
 * @return If the registered string can be parsed without error (as
 * shown by all @ref parser_parseToken() calls returning 0), 0 is
 * returned. Otherwise the return value of the first @ref
 * parser_parseToken() call different from 0 is returned.
 *
 * @see strtok_r(3)
 */
int parser_parseString(char* token, parser_t* parser);

/**
 * @brief Remove comment.
 *
 * Remove comments from line @a line. A comment starts with a hash
 * ('#') character and ends at the end of the line, i.e. the whole
 * rest of the line following the hash character will be omitted.
 *
 * Hash characters within quoted or double qouted parts of the line
 * will be ignored.
 *
 * This function might modify the character array @a line, i.e. it
 * will replace the hash character delimiting the comment with a null
 * ('\0') character throwing away the whole rest of @a line.
 *
 * @param line The line from which comments shall be removed.
 *
 * @return No return value.
 */
void parser_removeComment(char* line);

/**
 * @brief Parses a character stream.
 *
 * Parses the character stream set via @ref parser_init() or @ref
 * parser_setFile() pursuant to the syntax given by @parser.
 *
 *
 * @param parser The parser syntax used for parsing by passing to @ref
 * parser_registerString() and @ref parser_parseString().
 *
 *
 * Parsing is done by reading whole lines from the character
 * stream. Following each line is parsed using @ref
 * parser_registerString() and @ref parser_parseString() calls.
 *
 * If @ref parser_parseString() returns 0, parsing goes on, otherwise
 * the return value of @ref parser_parseString() is returned.
 *
 * @return If the character stream can be parsed without error (as
 * shown by all parser_parseString() calls returning 0), 0 is
 * returned. Otherwise the return value of the first
 * parser_parseString() call displaying an error is returned.
 */
int parser_parseFile(parser_t* parser);

/** private logger to use */
extern logger_t *parserlogger;

/**
 * Various message classes for logging. These define the different
 * bits of the debug-mask set via @ref parser_setDebugMask().
 */
typedef enum {
    PARSER_LOG_ECHO = 0x01000000, /**< Echo each line to parse */
    PARSER_LOG_FILE = 0x02000000, /**< logs concerning the file to parse */
    PARSER_LOG_CMNT = 0x04000000, /**< Comment handling */
    PARSER_LOG_NODE = 0x08000000, /**< Info concerning each node */
    PARSER_LOG_RES =  0x10000000, /**< Info on various resource to define */
    PARSER_LOG_VERB = 0x20000000, /**< more verbose stuff */
} parser_log_key_t;

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
int parser_error(char* token);

/**
 * @brief Query the debug-mask.
 *
 * Get the debug-mask of the parser module.
 *
 * @return The actual debug-mask is returned.
 *
 * @see parser_setDebugMask()
 */
int32_t parser_getDebugMask(void);

/**
 * @brief Set the debug-mask.
 *
 * Set the log-mask of the parser's logging facility to @a mask. @a
 * mask is a bit-wise OR of the different keys defined within @ref
 * parser_log_key_t.
 *
 * @param mask The debug-mask to set.
 *
 * @return No return value.
 *
 * @see parser_getDebugMask()
 */
void parser_setDebugMask(int32_t mask);

/**
 * @brief Print out a comment.
 *
 * Print out a comment concerning actual parsing. The @a comment will
 * be prepended with the current line number the parser acts at while
 * the comment is launched.
 *
 * This is a wrapper to @ref logger_print().
 *
 * The message is only put out if either:
 *
 * - the key @a key bitwise or'ed with the parser's current debug-mask
 * set via @ref setDebugMask() is different form zero, or
 *
 * - the key @a key is -1.
 *
 * Thus all messages with @a key set to -1 are put out always,
 * independently of the choice of @a parser's mask. Therefor critical
 * messages of general interest should be but out with @a key set to
 * this value.
 *
 * @param key The key to use in order to decide if anything is put out.
 *
 * @param format The format to be used in order to produce output. The
 * syntax used is according to the one defined for the @ref printf()
 * family of functions from the C standard. This string will also
 * define the further parameters to be expected.
 *
 * @return No return value.
 *
 * @see logger_print(), parser_getDebugLevel(), parser_setDebugLevel()
 */
#define parser_comment(...) if (parserlogger)	\
	logger_print(parserlogger, __VA_ARGS__)

/**
 * @brief Print a warn-messages and exit.
 *
 * Print a message like from @ref logger_warn(), but gives this
 * message always, i.e no comparison to @a logger's mask. Furthermore
 * calls exit() afterwards.
 *
 * This is mainly a wrapper to @ref logger_exit().
 *
 * @param errorno Error code describing the error string to append to
 * the message. If set to 0, no string is appended.
 *
 * @param format The format to be used in order to produce output.
 *
 * @return No return value.
 *
 * @see logger_exit(), exit()
 */
void parser_exit(int errorno, char* format, ...)
    __attribute__((format(printf,2,3),noreturn));

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
char* parser_getString(void);

/**
 * @brief Get a quoted of whitespace delemited string.
 *
 * Get a quoted or whitespace delemited string from the character
 * array passed to @ref parser_parseString(). Depending on the first
 * non-whitespace character this is done via strtok(NULL, " \t\n"),
 * strtok(NULL, "\"") or strtok(NULL, "\'").
 *
 * @return The result of strtok(NULL, " \t\n"), strtok(NULL, "\"") or
 * strtok(NULL, "\'") is returned.
 */
char* parser_getQuotedString(void);

/**
 * @brief Get the rest of the string to parse.
 *
 * Get the rest of the string to parse passed to @ref
 * parser_parseString() using strtok(NULL, "\n").
 *
 * @return The result of strtok(NULL, "\n") is returned.
 */
char* parser_getLine(void);

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
int parser_getComment(char* token);

/**
 * @brief Get a number.
 *
 * Get a number from the character array @a token and assign it to @a
 * val. If @a token does not contain a valid number, @a val remains
 * unchanged.
 *
 * @param token The character array that contains the number.
 *
 * @param val Pointer to the value to get.
 *
 * @return On success 0 is returned, or -1 otherwise.
 */
int parser_getNumber(char *token, long *val);

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
char* parser_getFilename(char* token, char* prefix, char* extradir);

/**
 * @brief Get a hostname.
 *
 * Get a hostname (i.e. a whitespace delimited string) from the
 * character array @a token and test if it can be resolved.
 *
 * @param token The character array that contains the hostname.
 *
 * @return On success, the resolved IP address of the hostname is
 * returned. Or 0, if an error occurred.
 */
in_addr_t parser_getHostname(char* token);

/**
 * @brief Get a numerical value.
 *
 * Get a numerical value from the character array @a token via @ref
 * parser_getNumber() and store it to @a *value. If an error occurred
 * (i.e. token contains no valid number), a message concerning @a
 * valname is produced and @a *value remains unchanged.
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
int parser_getNumValue(char* token, int* value, char* valname);

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
int parser_getBool(char* token, int* value, char* valname);

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
int parser_parseOn(char* line, parser_t* parser);

/**
 * @brief Shutdown the parser module.
 *
 * Shutdown the parser module and release all ressources occupied.
 *
 * Once the module is shut down all further calls to functions of the
 * parser module gives unspecifie results.
 *
 * @return No return value.
 */
void parser_finalize(void);

/**
 * @brief Update hash from a given line
 *
 * @param hashVal receives the updated hash value
 *
 * @param line buffer add to the hash calculation
 */
void parser_updateHash(uint32_t *hashVal, char *line);

#endif /* __PARSING_H */
