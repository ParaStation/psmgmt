
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
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
static char *arg_lickey=0;
static char *arg_routingtable=0;

void do_setlickey()
{
    if (PSHALSYSSetLicKey(arg_lickey)) {
	printf("SetLicKey failed\n");
	exit(1);
    }
}

void do_setid()
{
    FILE *rt;
    if (PSHALSYSSetID(arg_id)) {
	printf("SetID failed\n");
	exit(1);
    }
    if (arg_routingtable) {
	char line[1000];
	rt=fopen(arg_routingtable,"r");
	if (!rt) {
	    perror("open routingtable");
	    exit(1);
	}
	if (!fgets(line,sizeof(line),rt)) {
	    perror("read routingtable");
	    exit(1);
	}
	if (strcmp(line,"routing table\n")) {
	    printf("error in routing table [%s]%s\n",arg_routingtable,line);
	    exit(1);
	}
	while (fgets(line,sizeof(line),rt)) {
	    int cnt;
	    PSHALSYSRouting_t r;
	    int R[8];
	    int src,dest;
	    int i;
	    cnt=sscanf(line,"%d %d %d %d %d %d %d %d %d %d",
		       &src,&dest,
		       &R[0],&R[1],&R[2],&R[3],
		       &R[4],&R[5],&R[6],&R[7]);
	    if (cnt>=3 && src==arg_id) {
		r.dstnode=dest;
		r.RLen=cnt-2;
		for (i=0; i<r.RLen; i++) {
		    r.Route[i] = R[i];
		}
		PSHALSYSSetRoutes(1,&r);
	    }
	}
    }
    
}

void do_setroute()
{
    int i;
    PSHALSYSRouting_t r;
    r.dstnode = arg_routenode;
    r.RLen = 0;
    for (i=0; i<8; i++) {
	r.Route[i] = arg_route[i];
	if (r.Route[i]!=-100) r.RLen++;
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
    while (0==PSHALSYS_GetMCPParam(i,&val,name20)) {
	printf("%3d  %20s %10d 0x%08x\n",i,name20,val,val);
	i++;
    }
}
    

void do_set_param()
{
    if (PSHALSYS_SetMCPParam( arg_param , arg_param_value)) {
	fprintf(stderr,"Set MCP parameter %d failed!\n", arg_param);
    } else {
	UINT32 val;
	char name20[20];
	PSHALSYS_GetMCPParam(arg_param,&val,name20);
	if ((UINT32)arg_param_value!=val){
	    fprintf(stderr,"Set MCP parameter %d (%20s) failed!\n",
		    arg_param,name20);
	}
    }
}


int main(int argc, char **argv)
{
    signal(SIGINT , cleanup);
    //printf(__DATE__" "__TIME__"\n");

    if (arg_parse(argc, argv,
		  "", "Usage: %s [options]", argv[0],
		  "", "This program set routes",
		  "-key %S",&arg_lickey,"Set license key",
		  "-id %d[%S]",&arg_id,&arg_routingtable,"Node ID [routingtable]",
		  "-r %d %d[%d[%d[%d[%d[%d[%d[%d]]]]]]]",
		  &arg_routenode,
		  &arg_route[0],&arg_route[1],&arg_route[2],&arg_route[3],
		  &arg_route[4],&arg_route[5],&arg_route[6],&arg_route[7],
		  "set route to node (node r0 r1 r2 r3 ..)",
		  "-qp",ARG_FLAG(&arg_queryparam),"query MCP parameter",
		  "-p %d %d",&arg_param,&arg_param_value,
		  "set MCP param paramNo to value",
		  "-sps %d",&arg_sps,"set small packet size (N2H)",
		  0) < 0){
        exit(1);
    }
    pshal_default_mcp=NULL;
    PSHALStartUp(0);

    if (arg_lickey != 0) do_setlickey();
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
