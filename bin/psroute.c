#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "ps_types.h"
#include "pshal.h"
#include "psm_mcpif.h"
//#include "mcpstructs.h"
#include <signal.h>
#include "arg.h"

void cleanup(int signal)
{
    PSHALSYSGetCounter(NULL);
    PSHALSYSGetMessage(NULL,NULL);
    exit(0);
}


static int arg_id=-1;
static int arg_routenode=-1;
static int arg_route[8]={-100,-100,-100,-100,-100,-100,-100,-100};
static int arg_sps=-1;
static int arg_queryparam=-1;
static int arg_param=-1;
static int arg_param_value=-1;
static char* arg_conffile=NULL;

void do_setid()
{
    if ( PSHALSYSSetID(arg_id) != 0 ){
	printf("SetID failed\n");
    }
}

void do_setroute()
{
    int i;
    PSHALSYSRouting_t r;
    r.dstnode = arg_routenode;
    r.RLen = 0;
    for (i=0;i<8;i++){
	r.Route[i] = arg_route[i];
	if (r.Route[i]!=-100)r.RLen++;
    }
    PSHALSYSSetRoutes(1,&r);
}

void do_sps()
{
    PSHALSYS_SetMCPParam(MCP_PARAM_SPS,arg_sps);
}

void do_query_param()
{
    int i=0;
    UINT32 val;
    char name20[20];
    while(0==PSHALSYS_GetMCPParam(i,&val,name20)){
	printf("%3d  %20s %10d 0x%08x\n",i,name20,val,val);
	i++;
    }
}
    

void do_set_param()
{
    if (PSHALSYS_SetMCPParam( arg_param , arg_param_value)){
	fprintf(stderr,"Set MCP parameter %d failed!\n");
    }else{
	int i=0;
	UINT32 val;
	char name20[20];
	PSHALSYS_GetMCPParam(arg_param,&val,name20);
	if ((UINT32)arg_param_value!=val){
	    fprintf(stderr,"Set MCP parameter %d (%20s) failed!\n",
		    arg_param,name20);
	}
    }
}

void do_parse_conffile(char * filename)
{
    FILE *cf = fopen(filename,"R");
    

}


int main(int argc, char **argv)
{
    struct psm_mcpif_mmap_struct * ms;
    int pshal_fd=0;
    int res;
    int i;
    int size;
    char mes[64];
    char buf[ 1*1024*1024 ];

    signal(SIGINT , cleanup);
    //printf(__DATE__" "__TIME__"\n");

    if (arg_parse(argc, argv,
		  "", "Usage: %s [options]", argv[0],
		  "", "This program set routes",
		  "-id %d",&arg_id,"Node ID",
		  "-r %d %d[%d[%d[%d[%d[%d[%d[%d]]]]]]]",
		  &arg_routenode,
		  &arg_route[0],&arg_route[1],&arg_route[2],&arg_route[3],
		  &arg_route[4],&arg_route[5],&arg_route[6],&arg_route[7],
		  "set route to node (node r0 r1 r2 r3 ..)",
		  "-qp",ARG_FLAG(&arg_queryparam),"query MCP parameter",
		  "-p %d %d",&arg_param,&arg_param_value,
		  "set MCP param paramNo to value",
		  "-sps %d",&arg_sps,"set small packet size (N2H)",
		  "-c %S",&arg_conffile,"configuration file",
		  0) < 0){
        exit(1);
    }


    if (arg_conffile) do_parse_conffile(arg_conffile);
    printf("ConfFile:%s\n",arg_conffile);
    PSHALStartUp(0);
    

    if (arg_id != -1) do_setid();
    if (arg_routenode != -1) do_setroute();
    if (arg_param != -1) do_set_param();
    if (arg_queryparam) do_query_param();
    if (arg_sps != -1) do_sps();

    return 0;
}




/*
 * Local Variables:
 *  compile-command: "make psconfig"
 *
 */
