#ifndef __PS_PLUGIN_LIB_HOSTLIST
#define __PS_PLUGIN_LIB_HOSTLIST

#include <stdint.h>

int range2List(char *prefix, char *range, char **list, size_t *size,
			uint32_t *count);
char *expandHostList(char *hostlist, uint32_t *count);

#endif
