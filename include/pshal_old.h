
#ifndef _PSHAL_H_
#define _PSHAL_H_

#include "psm_ioctl.h"

int PSHALSYS_GetDevHandle(void);
int PSHAL_GetCounter( PSHAL_MCP_COUNT * counter );
int PSHAL_GetRTC( void );
int PSHAL_GetISR( void );
char * PSHAL_MCPName(void);
void PSHAL_StartUp(int syslogerror);
void PSHAL_XXStartUp(void);
int PSHALSYS_SetID(int ID);
int PSHAL_OpenContext(void);
extern struct psm_mcpif_mmap_struct * pshal_mcpif;
extern char * pshal_default_mcp;

#endif /* _PSHAL_H */








