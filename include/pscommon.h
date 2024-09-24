/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Functions used in user-programs and daemon.
 */
#ifndef __PSCOMMON_H
#define __PSCOMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "logging.h"  // IWYU pragma: export
#include "psnodes.h"  // IWYU pragma: export
#include "pstask.h"   // IWYU pragma: export

/** Get the smaller value of a and b */
#define MIN(a,b) (((a) < (b)) ? (a) : (b))

/** The logger we use inside PSC */
extern logger_t PSC_logger;

/**
 * @brief Determines the number of nodes of the cluster.
 *
 * Determines the number of nodes which build the cluster.
 *
 * @return The number of nodes is returned, or -1, if the cluster is
 * not already initialized.
 */
PSnodes_ID_t PSC_getNrOfNodes(void);

/**
 * @brief Sets the number of nodes of the cluster.
 *
 * Sets the number of nodes which build the cluster.
 *
 * @param numNodes The number of nodes which will build the cluster.
 *
 * @return No return value.
 */
void PSC_setNrOfNodes(PSnodes_ID_t numNodes);

/**
 * @brief Test ParaStation ID
 *
 * Test the validity of the ParaStation node ID @a id.
 *
 * @param id ParaStation node ID to check
 *
 * @return Return true if @a id is valid, or false if it is out of
 * range
 */
bool PSC_validNode(PSnodes_ID_t id);

/**
 * @brief Determines the local ID within the cluster.
 *
 * Determines the ID (aka rank) of the local node within the cluster.
 *
 * @return The local ID of the node is returned, or -1, if the cluster is
 * not already initialized.
 */
PSnodes_ID_t PSC_getMyID(void);

/**
 * @brief Sets the local ID within the cluster.
 *
 * Sets the ID (aka rank) of the local node within the cluster to @a
 * id. It is dangerous to use this routine, since no testing on the
 * uniqueness of @a id within the cluster is performed.
 *
 * @param id The ID of the local nodes within the cluster.
 *
 * @return No return value.
 */
void PSC_setMyID(PSnodes_ID_t id);

/**
 * @brief Computes a task ID from process ID and node number.
 *
 * Computes the clusterwide unique task ID of a process from its
 * process ID @a pid and the node number @a node the process resides
 * on.
 *
 *
 * @param node The node number the process resides on. If -1, the
 * actual local node number is used.
 *
 * @param pid The process ID of the process, the task ID is computed for.
 *
 *
 * @return The unique task ID is returned.
 */
PStask_ID_t PSC_getTID(PSnodes_ID_t node, pid_t pid);

/**
 * @brief Computes the node number from a task ID.
 *
 * Determines on which node the process with the clusterwide unique
 * task ID @a tid resides.
 *
 * @param tid The unique task ID of the process.
 *
 * @return The node number on which the process resides is returned.
 */
PSnodes_ID_t PSC_getID(PStask_ID_t tid);

/**
 * @brief Computes the process ID from a task ID.
 *
 * Determines the process ID of the process with the clusterwide
 * unique task ID @a tid.
 *
 * @param tid The unique task ID of the process.
 *
 * @return The process ID of the process is returned.
 */
pid_t PSC_getPID(PStask_ID_t tid);

/**
 * @brief Mark the local process to be a ParaStation daemon
 *
 * Mark the actual process to a ParaStation daemon if @a flag is
 * true. If the current process is marked, the determination of @ref
 * PSC_getMyTID() is modified in a way that the process ID part of the
 * task ID is set to 0.
 *
 * Calling this function implies to reset MyTID's cached value via
 * @ref PSC_resetMyTID().
 *
 * @return No return value
 *
 * @see PSC_getMyTID(), PSC_resetMyTID(), PSC_isDaemon()
 */
void PSC_setDaemonFlag(bool flag);

/**
 * @brief Check if the local process is marked to be a ParaStation daemon
 *
 * Check if the local process was marked to be a ParaStation daemon by
 * calling @ref PSC_setDaemonFlag() accordingly. This might shortcut
 * the determination from the process ID part of MyTID.
 *
 * @return Returns the last flag passed via @ref PSC_setDaemonFlag()
 */
bool PSC_isDaemon(void);

/**
 * @brief Determines the task ID of the actual process
 *
 * Determines the clusterwide unique task ID of the actual process. If
 * @ref PSC_setDaemonFlag() was called with parameter true before, the
 * process is assumed to be a ParaStation daemon and the process ID
 * within the task ID is set to 0.
 *
 * @return The unique task ID of the actual process is returned; or -1
 * if the cluster is not yet fully initialized
 *
 * @see PSC_setDaemonFlag()
 */
PStask_ID_t PSC_getMyTID(void);

/**
 * @brief Reset MyTID
 *
 * MyTID, i.e. the value returned by PSC_getMyTID() is cashed withing
 * that function. Thus, whenever a fork happened, this cashed value
 * has to be reset. This is done by calling this function.
 *
 * Afterwards the cashed value is deleted and will be renewed with the
 * now correct values during the next call of PSC_getMyTID().
 *
 * @return No return value
 *
 * @see PSC_getMyTID()
 */
void PSC_resetMyTID(void);

/**
 * @brief Get string describing the task ID
 *
 * Get a string describing the task ID @a tid. The returned pointer
 * leads to a static character array that contains the
 * description. Subsequent calls to @ref PSC_printTID() will change the
 * content of this array. Therefore the result is not what you expect
 * if more than one call of this function is made within a single
 * argument-list of printf(3) and friends.
 *
 * @param tid Task ID to describe
 *
 * @return A pointer to a static character array containing task ID's
 * description. Do not try to free(2) this array.
 */
char *PSC_printTID(PStask_ID_t tid);

/**
 * @brief Get a string describing the current version
 *
 * Get a static string describing the current version of the whole
 * psmgmt installation. The string is of the form
 * VERSION_psmgmt"-"RELEASE_psmgmt and describes the situation at
 * compile time.
 *
 * @return Pointer to a static character array describing the current
 * version
 */
const char* PSC_getVersionStr(void);

/**
 * @brief Start a ParaStation daemon
 *
 * Try to start the ParaStation daemon on the host with IP address @a
 * hostaddr. The IP address has to be given in network byte order.
 *
 * The (x)inetd(8) service or the corresponding systemd(8) unit for
 * the psidstarter socket has to be configured appropriately and must
 * be enabled on the destination node.
 *
 * @param hostaddr IP address of the node on which to start the daemon
 *
 * @return No return value
 */
void PSC_startDaemon(in_addr_t hostaddr);

/**
 * @brief Initialize the PSC logging facility
 *
 * Initialize the PSC logging facility. This is mainly a wrapper to
 * @ref logger_new().
 *
 * @param logfile Alternative file to use for logging.
 *
 * @return No return value
 *
 * If @a logfile is NULL, syslog() will be used for any
 * output. Otherwise @a logfile will be used.
 *
 * @ref PSC_logInitialized() might be used in order to test, if the
 * PSC logging facility is already initialized.
 *
 * @see logger_new(), syslog(3)
 */
void PSC_initLog(FILE *logfile);

/**
 * @brief Test initialization of PSC logging facility
 *
 * Test if the PSC logging facility was initialized by calling @ref
 * PSC_initLog().
 *
 * @return If PSC_initLog() was called before, true is returned; or
 * false otherwise
 *
 * @see PSC_initLog()
 */
bool PSC_logInitialized(void);

/**
 * @brief Get the log-mask of the PSC logging facility.
 *
 * Get the actual log-mask of the PSC logging facility. This is
 * mainly a wrapper to @ref logger_getMask().
 *
 * @return The actual log-mask is returned.
 *
 * @see PSC_setDebugMask(), logger_getMask()
 */
int32_t PSC_getDebugMask(void);

/**
 * @brief Set the log-mask of the PSC logging facility.
 *
 * Set the log-mask of the PSC logging facility to @a mask. @a mask is
 * a bit-wise OR of the different keys defined within @ref
 * PSC_log_key_t.
 *
 * This is mainly a wrapper to @ref logger_setMask().
 *
 * @param mask The log-mask to be set.
 *
 * @return No return value.
 *
 * @see PSC_setDebugMask(), logger_setMask()
 */
void PSC_setDebugMask(int32_t mask);

/**
 * Print a log messages via PSC's logging facility @a PSC_logger .
 *
 * This is a wrapper to @ref logger_print().
 *
 * @see logger_print()
 */
#define PSC_log(...) logger_print(PSC_logger, __VA_ARGS__)
#define PSC_flog(...) logger_funcprint(PSC_logger, __func__, -1, __VA_ARGS__)

#define PSC_dbg(...) logger_print(PSC_logger, __VA_ARGS__)
#define PSC_fdbg(...) logger_funcprint(PSC_logger, __func__, __VA_ARGS__)

/**
 * Print a warn messages via PSC's logging facility @a PSC_logger .
 *
 * This is a wrapper to @ref logger_warn().
 *
 * @see logger_warn()
 */
#define PSC_warn(...) logger_warn(PSC_logger, __VA_ARGS__)

/**
 * Print a warn messages via PSC's logging facility @a PSC_logger and exit.
 *
 * This is a wrapper to @ref logger_exit().
 *
 * @see logger_exit()
 */
#define PSC_exit(...) logger_exit(PSC_logger, __VA_ARGS__)

/**
 * @brief Finalize PSC's logging facility.
 *
 * Finalize PSC's logging facility. This is mainly a wrapper to
 * @ref logger_finalize().
 *
 * @return No return value.
 *
 * @see PSC_initLog(), logger_finalize()
 */
void PSC_finalizeLog(void);

/**
 * Various message classes for logging. These define the different
 * bits of the debug-mask set via @ref PSC_setDebugMask().
 */
typedef enum {
    PSC_LOG_PART = 0x0001, /**< partitioning functions (i.e. PSpart_()) */
    PSC_LOG_TASK = 0x0002, /**< task structure handling (i.e. PStask_()) */
    PSC_LOG_VERB = 0x0004, /**< Various, less interesting messages */
    PSC_LOG_COMM = 0x0008, /**< communication, i.e. serialization functions */
} PSC_log_key_t;

/**
 * @brief Get the ParaStation installation directory.
 *
 * Get the ParaStation installation directory, i.e. the directory
 * containing all the ParaStation stuff. This function might try to
 * lookup the installation directory by itself -- in which case it
 * tests $(prefix) -- or it might get a @a hint. In the
 * latter case any previous installation directory is discarded.
 *
 * In any case it tries to find the 'bin/psilogger' which is used as a
 * landmark in order to identify the actual presence of ParaStation
 * within this directory.
 *
 * @param hint Hint for the location to search for 'bin/psilogger'
 *
 * @return On success, i.e. if ParaStation was found, the ParaStation
 * installation directory is returned. Otherwise an empty string is
 * returned.
 */
char *PSC_lookupInstalldir(char *hint);

/**
 * @brief Get current working directory
 *
 * Get the current working directory. If @a ext is provided and does
 * not start with '/', it will be appended to the determined string.
 *
 * The strategy to determine the current working directory is to
 * firstly look for the PWD environment variable and if this is not
 * present, to call getcwd(3).
 *
 * @param ext The extension to append to determined directory.
 *
 * @return On success, a pointer to a character array containing the
 * extended working directory is returned. This array is allocated via
 * malloc() and should be free()ed by the user when it is no longer
 * needed. In case of error NULL is returned and errno is set
 * appropriately.
 */
char *PSC_getwd(const char *ext);

/**
 * @brief Get a port entry
 *
 * Get the TCP port number associated with the service entry @a name
 * via the getservbyname() call. If @a name could not be resolved, is
 * @a def as the default port number.
 *
 * @param name The name of the service entry to lookup.
 *
 * @param def The default value to return, if the lookup fails.
 *
 * @return On success, i.e. if the service entry @name could be
 * resolved, the corresponding port number is returned. Otherwise @a
 * def is returned. If the resolved port number is identical to @a
 * def, the failure of the lookup is indiscernible.
 */
int PSC_getServicePort(char* name , int def);

/**
 * @brief Test if IP address is assigned to the local node
 *
 * Test if the IP address @a ipaddr is assigned to on one of the local
 * network devices. Internally a list of assigned IP addresses is
 * created on the fly during the first call of this function. This
 * list will never be discarded. Therefore, changes to the local IP
 * configuration during the runtime of pscommon are never detected.
 *
 * This function might call exit() if an error occurred during
 * determination of local IP addresses.
 *
 * @param ipaddr The IP address to search for
 *
 * @return If the IP address @a ipaddr is assigned to one of the local
 * network devices, true is returned. Or false if the address is not
 * found.
 */
bool PSC_isLocalIP(in_addr_t ipaddr);

/**
 * @brief Get nodelist from string
 *
 * Parse the string @a descr describing a nodelist and create and
 * return the corresponding nodelist. The resulting nodelist is an
 * array of size PSC_getNrOfNodes(), each bool flagging if the
 * corresponding nodes was found within @a descr.
 *
 * The nodelist is returned in a statically allocated buffer, which
 * subsequent calls will overwrite.
 *
 * @param descr Character string describing a list of nodes. It must
 * be of the form n[-m]{,o[-p]}*, where n, m, o and p are
 * numbers. Each number might be in decimal, octal or hexadecimal
 * notation. Octal notation is marked by a leading 0, hexadecimal
 * notation by a leading 0x.
 *
 * @return On success, an array indexed by node ID flagging if the
 * node is contained in @a descr is returned. Or NULL if an (parsing-)
 * error occurred.
 */
bool * PSC_parseNodelist(char* descr);

/**
 * @brief Print a nodelist description
 *
 * Print a string to stdout that describes the nodelist stored in @a
 * nl. The printed string might be parsed by @ref PSC_parseNodelist()
 * and the returned result will be identical to @a nl.
 *
 * The string printed out is expected to be the shortest description
 * of the nodelist @a nl that is possible.
 *
 * @param nl A nodelist as returned by the PSC_parseNodelist() function
 *
 * @return No return value
 *
 * @see PSC_parseNodelist()
 */
void PSC_printNodelist(bool* nl);

/**
 * @brief Concatenate strings.
 *
 * Concatenate two or more strings and return a pointer to the
 * resulting string. Memory for the new string is obtained via @ref
 * malloc(), and must be freed with using @ref free(). The last
 * argument passed needs always to be 0L.
 *
 * @param str Start of the concatenated string. More strings to follow.
 *
 * @return Upon success, a pointer to the concatenated string is
 * returned, or NULL, if insufficient memory was available.
 */
char * __PSC_concat(const char *str, ...);

/**
 * Make use of @ref __PSC_concat more convenient by implicit adding 0L
 */
#define PSC_concat(...) __PSC_concat(__VA_ARGS__, 0L)


/**
 * @brief Save some space to modify the process title
 *
 * Save some save to be able to modify the process title that appears
 * on the ps(1) command later on via @ref PSC_setProcTitle(). For this
 * the process memory used for arguments (argv) and the process
 * environment is analyzed and the information is stored internally
 * for later use by PSC_setProcTitle(). No actual changes to the
 * process memory are made, thus, it is save to call this function
 * before the argument-vector parameters are evaluated.
 *
 * In order to do the analysis the program's argument count @a argc
 * and the actual argument vector @a argv are required. Since the
 * memory occupied by the environment might be reserved for the
 * changed process title, too, the flag @a saveEnv will trigger to
 * copy the original environment to a save place if access to the
 * environment is required later on.
 *
 * If the environment is not saved, the functions getenv(3)/setenv(3)
 * must not be used afterwards.
 *
 * @param argc The number of arguments to be used by the modified
 * process title.
 *
 * @param argv The arguments to be used. This has to be the original
 * argument vector passed to the applications main() function.
 *
 * @param saveEnv If set to true, the process' environment will be
 * saved. Otherwise the environment will be overwritten.
 *
 * @return No return value.
 *
 * @see PSC_setProcTitle()
 */
void PSC_saveTitleSpace(int argc, const char **argv, int saveEnv);

/**
 * @brief Set the process title.
 *
 * Set the process title that appears on the ps(1) command to @a
 * title. This will use the process memory as described in @ref
 * PSC_saveTitleSpace().
 *
 *  Actually, this function will be called in order to reserve space
 * for the modified process title if not done before. Therefore, the
 * @a argc, @a argv and @a saveEnv parameters are required in order to
 * be passed to @ref PSC_saveTitleSpace(). Nevertheless, these
 * parameter are only relevant of @ref PSC_saveTitleSpace() was not
 * called before.
 *
 * @param argc The number of arguments to be modified. Will be passed
 * to @ref PSC_saveTitleSpace().
 *
 * @param argv The arguments to be modified.  Will be passed to @ref
 * PSC_saveTitleSpace().
 *
 * @param title The new process title.
 *
 * @param saveEnv Flag to be passed to @ref PSC_saveTitleSpace().
 *
 * @return Upon success 1 is returned. Or 0 if an error occurred.
 *
 * @see PSC_saveTitleSpace()
 */
int PSC_setProcTitle(int argc, const char **argv, char *title, int saveEnv);

/**
 * @brief Get screen width.
 *
 * Get the screen width of the terminal stdout is connected to.
 *
 * If the TIOCGWINSZ @ref ioctl() is available, it is used to
 * determine the width. Otherwise the COLUMNS environment variable is
 * used to identify the size.
 *
 * If the determined width is smaller than 60, it is set to this
 * minimum value.
 *
 * If both methods cited above failed, the width is set to the default
 * size of 80.
 *
 *
 * @return On success, the actual screen size is returned. If the
 * determination of the current screen size failed, the default width
 * 80 is passed to the calling function. If the determined width is
 * too small, the minimal width 60 is returned.
 *
 * @see ioctl()
 */
int PSC_getWidth(void);

/**
 * @brief Register signal handler
 *
 * Register the signal handler @a handler to the signal @a signum. This
 * is basically a replacement for the inadvisable @ref signal(). It
 * utilizes the now preferred @ref sigaction().
 *
 * @param signum Number of the signal to be handled
 *
 * @param handler Function to call when signal @a signum is delivered
 * to the process
 *
 * @return Return the previous value of the signal handler, or SIG_ERR
 * on error. In the event of an error, errno is set to indicate the
 * cause.
 */
void (*PSC_setSigHandler(int signum, void handler(int)))(int);

/**
 * @brief Number from string
 *
 * Get a number from the character array @a numStr and assign it to @a
 * val. If @a numStr does not contain a valid number, @a val remains
 * unchanged.
 *
 * @param numStr String containing the number
 *
 * @param val Pointer to the value to get
 *
 * @return On success 0 is returned, or -1 otherwise.
 */
int PSC_numFromString(const char *numStr, long *val);

/**
 * @brief User ID from string
 *
 * Resolve the user ID from the string @a user. This takes also the
 * string "any" into account which is resolved to -1.
 *
 * @param user String describing the user to resolve
 *
 * @return If the string @a user hints to a known user, its user ID is
 * returned; if @a user is "any", -1 is returned; or -2 in case of an
 * error, i.e. an unknown user; in the last case, errno is set to
 * indicate the cause
 */
uid_t PSC_uidFromString(const char *user);

/**
 * @brief Group ID from string
 *
 * Resolve the group ID from the string @a group. This takes also the
 * string "any" into account which is resolved to -1.
 *
 * @param user String describing the group to resolve
 *
 * @return If the string @a group hints to a known group, its group ID
 * is returned; if @a group is "any", -1 is returned; or -2 in case of
 * an error, i.e. an unknown group; in the last case, errno is set to
 * indicate the cause
 */
gid_t PSC_gidFromString(const char *group);

/**
 * @brief Create user-string
 *
 * Create a string describing the user identified by the user ID @a
 * uid. This takes the special value -1 into account, for which "ANY"
 * is returned. If the user is unknown, i.e. @a uid cannot be
 * resolved, a copy of the string "unknown" is returned.
 *
 * @param uid User ID of the user to describe
 *
 * @return Pointer to a new string describing the user; memory for the
 * new string is obtained via @ref malloc(), and must be freed with
 * @ref free();
 */
char* PSC_userFromUID(int uid);

/**
 * @brief Create group-string
 *
 * Create a string describing the group identified by the group ID @a
 * gid. This takes the special value -1 into account, for which "ANY"
 * is returned. If the group is unknown, i.e. @a gid cannot be
 * resolved, a copy of the string "unknown" is returned.
 *
 * @param gid Group ID of the group to describe
 *
 * @return Pointer to a new string describing the group; memory for
 * the new string is obtained via @ref malloc(), and must be freed
 * with @ref free();
 */
char* PSC_groupFromGID(int gid);

/**
 * @brief Search the user database for a name
 *
 * Search the user database for a name and use the provided buffer to
 * store the data. The memory for the buffer is allocated using @ref
 * malloc() and the caller is responsible to free it using @ref
 * free().
 *
 * @param user The user to search
 *
 * @param pwBuf The buffer to store the database entries
 *
 * @return Returns the requested passwd structure holding the
 * user information or NULL on error; in the event of an error,
 * errno is set to indicate the cause
 */
struct passwd *PSC_getpwnamBuf(const char *user, char **pwBuf);

/**
 * @brief Search the user database for a uid
 *
 * Search the user database for a user ID and use the provided buffer to
 * store the data. The memory for the buffer is allocated using @ref
 * malloc() and the caller is responsible to free it using @ref
 * free().
 *
 * @param uid The user ID to search
 *
 * @param pwBuf The buffer to store the database entries
 *
 * @return Returns the requested passwd structure holding the
 * user information or NULL on error; in the event of an error,
 * errno is set to indicate the cause
 */
struct passwd *PSC_getpwuidBuf(uid_t uid, char **pwBuf);

/**
 * @brief Visitor function
 *
 * Visitor function utilized by @ref int PSC_traverseHostInfo() in
 * order to visit each item of type AF_INET in the linked list
 * returned by @ref getaddrinfo().
 *
 * The parameters are as follows: @a saddr is a pointer to the struct
 * sockaddr contained in the AF_INET item currently visited. @a info
 * points to the additional information passed to @ref
 * PSC_traverseHostInfo() in order to be forwarded to each object.
 *
 * If the visitor function returns true, this result will be stored to
 * the memory @ref traverseHostInfo()'s @a match parameter points to
 * and the traversal will be interrupted. Thus, @ref
 * PSC_traverseHostInfo() will return to its calling function.
 */
typedef bool hostInfoVisitor_t(struct sockaddr_in * saddr, void *info);

/**
 * @brief Traverse hostinfo elements provided by @ref getaddrinfo()
 *
 * Traverse all hostinfo elements of type AF_INET in the linked list
 * returned by @ref getaddrinfo() when @a host is passed as its node
 * parameter. For each according item of the link list @a visitor is
 * called with the item's ai_addr element as the first parameter and
 * @a info as the second parameter. Thus, @a info might be used to
 * pass additional information to the @a visitor function.
 *
 * If @a visitor returns true, the return value will be stored to the
 * memory @a match points to and the traversal will be stopped
 * immediately and true is returned to the calling function.
 *
 * @a match might be NULL. In this case the caller of this function
 * will get no direct information on a possible matches. If @a match
 * points to some memory, this memory will be initialized as false!
 *
 * @param host Hostname passed to @ref getaddrinfo() as the node
 * parameter
 *
 * @param visitor Visitor function to be called for each item of type
 * AF_INET in the linked list provided by @ref getaddrinfo()
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting all items of the linked list
 *
 * @param match Points to memory indicating a match (i.e. @a visitor
 * has returned true) has happened during the traversal; might be NULL
 *
 * @return Returns the return value of the utilized @ref getaddrinfo()
 * call. Thus, if getaddrinfo() returned successfully and the linked
 * list was traversed (with or without match), 0 is
 * returned. Otherwise, @ref getaddrinfo()'s error code is returned
 * that might be passed to @ref gai_strerror().
 */
int PSC_traverseHostInfo(const char *host, hostInfoVisitor_t visitor,
			 void *info, bool *match);

/**
 * @brief Switch effective user
 *
 * Switch the effective user but not the real user. This allows to temporary
 * drop root privileged and to reclaim them at a later stage. To achieve this
 * the effective user ID and group ID are modified. If @a username is not NULL
 * the supplemental group IDs are also set. Otherwise the supplemental group IDs
 * are cleared.
 *
 * It is *important* to ensure the function completed without errors
 * by checking the return value.
 *
 * @param username Username of the new effective user
 *
 * @param uid New effective user ID
 *
 * @param gid New effective group ID
 *
 * @return Returns true on success otherwise false is returned
 */
__attribute__ ((__warn_unused_result__))
bool PSC_switchEffectiveUser(char *username, uid_t uid, gid_t gid);

#endif  /* __PSCOMMON_H */
