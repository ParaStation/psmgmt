
#ifndef _MCPSTRUCTS_H_
#define _MCPSTRUCTS_H_

#include "ps_types.h"
#include "pshal.h"
#include "psm_mcpif.h"
#include "mcpif.h"
#include "pvar.h"

//  inline void StructsInit(char *structsfile){
//      if (structsfile){
//  	PVarInit( structsfile );
//      }else{
//  	PVarInit( "mcpstructs.o" );
//      }
//  }

typedef struct head_alpha_T{
    PSHALRecvHeader_t   HALHeader;
    /* Port Header */
    UINT16		MessageID;   /* 12 0*/
    UINT32              FragOffset;  /* 16 4*/
    UINT32              MessageSize; /* 20 8*/
    /* MPI Header */
    INT8		type;        /* 24 */
    INT8 _fill1_;
    INT16		tag;         /* 26 */
    INT16		context_id;  /* 28 */
    INT16		src_lrank;   /* 30 */
    UINT16		uniq;        /* 32 */
    INT16 _fill2_;
    INT32		length;      /* 36 */
}head_alpha_t;

#ifndef MCPSTRUCTS_FIRST_RUN

#include "mcpstructs_stabs.h"

inline void StructsInit(void){
    PVarInitH( pvar_stabs );
}

#endif /* MCPSTRUCTS_FIRST_RUN */

#endif
