#ifndef __PS_PLUGIN_LIB_HELPER
#define __PS_PLUGIN_LIB_HELPER

#include "psnodes.h"

/**
 * @brief Remove a directory recursive.
 *
 * @param directory The directory to remove.
 *
 * @param root Also delete the root directory.
 *
 * @return Returns 0 on error and 1 on success.
 */
int removeDir(char *directory, int root);

/**
 * @brief Get the PS Node ID by hostname.
 *
 * @param host The hostname to get the nodeID for.
 *
 * @return Returns the requested nodeID or -1 on error.
 */
PSnodes_ID_t getNodeIDbyName(char *host);

/**
 * @brief Get the hostname from a PS nodeID.
 *
 * @param id The nodeID to lookup the hostname for.
 *
 * @return Returns the requested hostname or NULL on error.
 */
const char *getHostnameByNodeId(PSnodes_ID_t id);

/**
 * @brief Block a signal.
 *
 * @param signal The signal to block.
 *
 * @param block Flag to block the signal if set to 1 or unblock it if set to 0.
 *
 * @return No return value.
 */
void blockSignal(int signal, int block);

char *trim(char *string);
char *ltrim(char *string);
char *rtrim(char *string);

#endif
