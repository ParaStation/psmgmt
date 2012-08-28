/*
 * ParaStation
 *
 * Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * \file
 * ParaStation parameter-space manager
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSPARAMSPACE_H
#define __PSPARAMSPACE_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

typedef char *PSPARM_setFunc_t(void *, char *);

typedef char *PSPARM_printFunc_t(void *);

/**
 * @brief Initialize parameter-space
 *
 * Initialize the parameter-space management framework.
 *
 * @return No return value.
 */
void PSPARM_init(void);

/**
 * @brief Finalize parameter-space
 *
 * Shutdown the parameter-space management framework. This includes
 * removing all parameters from the parameter-space.
 * 
 * @return No return value.
 */
void PSPARM_finalize(void);


/**
 * @brief Register new parameter
 *
 * Register the new parameter @a name to the parameter-space. In order
 * to manage the parameter, various handling functions, i.e. @a
 * setFunc, @a printFunc and @a helpFunc might be registered,
 * too. They will be called to modifiy the parameter's value, to print
 * its value or to get some help-statement concerning the parameter,
 * respectively.
 *
 * @a data might point to some additional information to be passed to
 * the handling functions as the first argument. Typically it will
 * point to the actual adress used to store the parameter's data.
 *
 * This function ensures the parameter to be unique. I.e. if @a name
 * was registered before and not yet removed, an error is returned.
 *
 * @param name The name of the parameter to register
 *
 * @param data Pointer to some additional data to be stored along the
 * parameter. Will be passed to @a setFunc, @a printFunc and @a
 * helpFunc as first parameter.
 *
 * @param setFunc Handling function called via @ref PSPARM_set() in
 * order to modify the parameter's value.
 *
 * @param printFunc Handling function called via @ref PSPARM_print() in
 * order to print the parameter's value.
 *
 * @param helpFunc Handling function called via @ref PSPARM_help() in
 * order to print some help-statement on the parameter's meaning.
 *
 * @return Upon success, 1 is returned. Or 0 otherwise.
 */
int PSPARM_register(char *name, void *data, PSPARM_setFunc_t setFunc,
		    PSPARM_printFunc_t printFunc, PSPARM_printFunc_t helpFunc);

/**
 * @brief Remove parameter
 *
 * Remove the parameter @a name from the parameter-space.
 *
 * If @a name is not found in the parameter-space, an error is emitted.
 *
 * @param name The name of the parameter to remove
 *
 * @return Upon success, 1 is returned. Or 0 otherwise.
 */
int PSPARM_remove(char *name);


/**
 * @brief Modify parameter
 *
 * Modify the value of the parameter @a name to @a value. Implicitely
 * this uses the parameter's set-method in order to do the change to
 * the actual value.
 *
 * If @a name is not known to the parameter-space or @a value cannot
 * be handled by the set-method a message is emitted to stderr.
 *
 * @param name The name of the parameter to be modified
 *
 * @param value The value to be set
 *
 * @return No return value.
 */
void PSPARM_set(char *name, char *value);

/**
 * @brief Get parameter
 *
 * Get the value of the parameter @a name. Implicitely this uses the
 * parameter's print-method in order to create a character string
 * containing the actual value.
 *
 * If @a name is not known to the parameter-space, the print-method is
 * not existing or some error occurs, NULL is returned. Otherwise, a
 * pointer to a character string containing the actual value is
 * returned. The calling function is responsible to free() this
 * buffer, once it is no longer used.
 *
 * @param name The name of the parameter of interest
 *
 * @return A pointer to the actual help message or NULL.
 */
char * PSPARM_get(char *name);

/**
 * @brief Print parameter
 *
 * Print the value of the parameter @a name to the file @a
 * file. Implicitely this uses the parameter's print-method in order
 * to create some output.
 *
 * If @a file is NULL, the corresponding output is sent to stdout.
 *
 * If @a name is not known to the parameter-space or the print-method
 * is not existing a message is emitted to stderr.
 *
 * @param file The file to use for output
 *
 * @param name The name of the parameter to be printed
 *
 * @return No return value.
 */
void PSPARM_print(FILE *file, char *name);

/**
 * @brief Get help on parameter
 *
 * Get a help-message on the parameter @a name. Implicitely this
 * uses the parameter's help-method in order to create some output.
 *
 * If @a name is not known to the parameter-space or the help-method
 * is not existing, NULL is returned. Otherwise, a pointer to a
 * character string containing the actual help-message is
 * returned. The calling function is responsible to free() this buffer,
 * once it is no longer used.
 *
 * @param name The name of the parameter of interest
 *
 * @return A pointer to the actual help message or NULL.
 */
char * PSPARM_get(char *name);

/**
 * @brief Print help on parameter
 *
 * Print some help-message on the parameter @a name. Implicitely this
 * uses the parameter's help-method in order to create some output.
 *
 * If @a name is not known to the parameter-space or the help-method
 * is not existing, a message is emitted to stderr.
 *
 * In order to create a proper formatting of the output it is
 * necessary to determine the width of the terminal to print
 * on. Therefore, it does not make much sene to print to a different
 * destination than stdout. If you require to print to a different
 * destination, try to get the corresponding help-messages using @ref
 * PSPARM_getHelp() and create the output manually.
 *
 * @param name The name of the parameter help is requested for
 *
 * @return No return value.
 */
void PSPARM_printHelp(char *name);

/**
 * Generic handling function to modify int parameters. Expects the @a
 * data argument of @ref PSPARM_register() to point to the variable
 * in use.
 */
extern PSPARM_setFunc_t *PSPARM_intSet;

/**
 * Generic handling function to print int parameters. Expects the @a
 * data argument of @ref PSPARM_register() to point to the variable
 * in use.
 */
extern PSPARM_printFunc_t *PSPARM_intPrint;

/**
 * Generic handling function to modify character-string
 * parameters. Expects the @a data argument of @ref
 * PSPARM_register() to point to the variable in use.
 */
extern PSPARM_setFunc_t *PSPARM_stringSet;

/**
 * Generic handling function to print character-string
 * parameters. Expects the @a data argument of @ref
 * PSPARM_register() to point to the variable in use.
 */
extern PSPARM_printFunc_t *PSPARM_stringPrint;

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSPARAMSPACE_H */
