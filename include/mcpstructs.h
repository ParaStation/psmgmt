
#ifndef _MCPSTRUCTS_H_
#define _MCPSTRUCTS_H_

#include "ps_types.h"
#include "pshal.h"
#include "psm_mcpif.h"
#include "mcpif.h"
#include "pvar.h"

inline void StructsInit(char *structsfile){
    if (structsfile){
	PVarInit( structsfile );
    }else{
	PVarInit( "mcpstructs.o" );
    }
}


#endif
