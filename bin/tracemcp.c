
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "ps_types.h"
#include "psm_ioctl.h"
#include "psm_const.h"
#include "mcpstructs.h"

#define PSM_DEVICE   "/dev/psm"

int print_structs = 1;
int run_fast = 0;

int main(int argc, char **argv){
    static int pshal_fd=0;
    PSHALMCPTrace_t trace;
    char buf[1024*1024];
    int i;
    int lasttracepos = -1;
    
    for (i=1;(i<argc)&&(strcmp(argv[i],"-s")!=0);i++){};
    if (i<argc){ print_structs=0;}

    for (i=1;(i<argc)&&(strcmp(argv[i],"-f")!=0);i++){};
    if (i<argc){ run_fast=1;}
    
    for (i=1;(i<argc)&&(strcmp(argv[i],"-h")!=0);i++){};
    if (i<argc){
	printf("-s Dont PrintStructs\n");
	printf("-f Run faster\n");
    }



    if (print_structs){
	StructsInit( argv[0] );
	PVarSwitchEndian = 1;
	PVarMaxArrayLen  = 10;
	PVarMaxCharLen   = 63;
    }
    
    
    if((pshal_fd = open(PSM_DEVICE,O_RDWR))==-1){  
	perror("Unable to open " PSM_DEVICE);
	exit(-1);
    }
    do {
	if (ioctl(pshal_fd, PSHAL_MCP_GETTRACE,&trace)!=0){
	    perror("Unable to call PSHAL_MCP_GETTRACE");
	    exit(-1);
	}
	if (trace.filename[0] == 0){
//	    printf(".");
	    if (!run_fast)
		usleep(40);
	}else{
	    if (lasttracepos == -1){
		lasttracepos = trace.tracepos-1;
	    }
	    if (++lasttracepos != trace.tracepos){
		/* Lost messages */
		printf("# ---- Skip %d lines -----\n",
		   trace.tracepos-lasttracepos);
		lasttracepos = trace.tracepos;
	    }

	    printf("%-20s%4d: %5d %10s %9u 0x%08x %9d \n",
		   trace.filename,trace.line,
		   trace.tracepos,trace.name,trace.time,
		   trace.val,trace.val);
	    fflush(stdout);
	}

	if (print_structs && (trace.name[0] == '(')){
	    // Print Struct;
	    int size;
	    char *name;
	    char *typename;
	    
	    typename = &trace.name[1];
	    if ((name = index(trace.name,')'))){
		*name=0;
		name++;
	    }
	    size = pvarsize(typename);
	    if (size){
		((UINT32 *)&buf)[0] = size;
		((UINT32 *)&buf)[1] = trace.val;
		ioctl(pshal_fd,PSHAL_DUMPSRAM,&buf);
		pvar(&buf,name,typename,0);
	    }
	}
//	usleep(40);
	 
    }while (1);

    return 0;
}

/*
 * Local Variables:
 *  compile-command: "make tracemcp"
 *
 */




