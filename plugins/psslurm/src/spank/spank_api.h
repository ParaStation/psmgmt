#ifndef __PS_SPANK_API
#define __PS_SPANK_API

#include <stdbool.h>

/**
 * @brief Init function for the global spank API
 *
 * @param verbose Enable verbose messages
 *
 * @return Returns true on success or false otherwise
 */
typedef bool psSpank_Init_t(bool);

psSpank_Init_t *psSpank_Init;

#endif
