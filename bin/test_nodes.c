/*
 * 2001-09-17  ParTec AG , Jens Hauke
 *
 */

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
#include "psport.h"
#include "pse.h"
#include <signal.h>
#include "arg.h"
#include <sys/time.h>

void cleanup(int signal)
{
    PSHALSYSGetCounter(NULL);
    PSHALSYSGetMessage(NULL,NULL);
    exit(0);
}

#define maxnp 1024

static int arg_np=-1;
static int arg_port=PSP_ANYPORT;
static int arg_cnt=1;
int conrecv[maxnp][maxnp];
int mapnode[maxnp];

int finish=0;

void time_handler(int signal)
{
    int j,k;
    fprintf(stdout,"\e[H");
    fprintf(stdout,"\e[2J");
    for (j=1;j<arg_np;j++){
	fprintf(stdout,"%3d(node %3d) ",j,mapnode[j]);
	for (k=1;k<arg_np;k++){
	    fprintf(stdout,"%1d ",conrecv[j][k]);
	}
	fprintf(stdout,"\n");
    }
//	fprintf(out,"cnt:  %d/%d\n",i,
//		end);
    fflush(stdout);
}

void run(int argc,char **argv,int np)
{
    struct {
	PSP_Header_t header;
	struct xdata_T{
	    int type;
	    int rank;
	    int from;
	    int to;
	    int port,node;
	}xdata;
    }head;
    PSP_PortH_t porth;
    PSP_RequestH_t Req;
    int mapport[np];
    int rank;
    int i,j,k,end;
    char filename[100];
    FILE *out;
    struct itimerval timer;
    if (PSP_Init()){
	perror("PSP_Init() failed!");
	exit(-1);
    }
    printf("Port Bind ");fflush(stdout);
    if (!(porth = PSP_OpenPort(arg_port))){
	perror("Cant bind port!");
	exit(-1);
    }
    for (i=0;i<np;i++){
	mapnode[i]=-1;
	mapport[i]=-1;
	for (j=0;j<np;j++){
	    conrecv[i][j]=0;
	}
    }
    
    mapport[0] = PSP_GetPortNo(porth);
    mapnode[0] = PSP_GetNodeID();
    rank =0;
    
    PSEinit(np,argc,argv,&mapnode[0],&mapport[0],&rank);

    if (rank>0){
//	sprintf(filename,"out.%d",rank);
//	out=fopen(filename,"w");
	out = stdout;
    }else{
	out=stdout;
	timer.it_interval.tv_sec=0;
	timer.it_interval.tv_usec=500*1000;
	timer.it_value.tv_sec=0;
	timer.it_value.tv_usec=500*1000;
	signal(SIGALRM,time_handler);
//	alarm(1);
	setitimer(ITIMER_REAL,&timer,0);
//	sleep(1);
    }
    mapnode[rank] = PSP_GetNodeID();
    mapport[rank] = PSP_GetPortNo(porth);

    fprintf(out,"master %d:%d my rank:%d\n",mapnode[0],mapport[0],rank);
    fflush(out);
    fprintf(out,"\e[2J");

    /* Send info to master */
    head.xdata.type=1;
    head.xdata.rank=rank;
    head.xdata.node=PSP_GetNodeID();
    head.xdata.port=PSP_GetPortNo(porth);
    Req = PSP_ISend(porth,0,0,&head.header,sizeof(struct xdata_T),mapnode[0],mapport[0]);
    PSP_Wait(porth,Req);
    
    end = (np) + (np+2)*(np)*arg_cnt;
//    for (i=0;(i<end)||(rank>0);i++){
    while(!finish){
	Req = PSP_IReceive(porth,0,0,&head.header,sizeof(struct xdata_T),0,0);
	PSP_Wait(porth,Req);
//	fprintf(out,"Recv (%d) from node %d (rank %d)\n",head.xdata.type,head.header.HALHeader.srcnode,
//		head.xdata.rank);
	switch (head.xdata.type){
	case 1:{
	    int r=head.xdata.rank;
	    mapnode[r] = head.xdata.node;
	    mapport[r] = head.xdata.port;
	    head.xdata.type=2;
	    for (j=0;j<np;j++){
		if ((mapnode[j]>=0) && (j != r) ){
		    for (k=0;k<arg_cnt;k++){
			Req = PSP_ISend(porth,0,0,&head.header,sizeof(struct xdata_T),mapnode[j],mapport[j]);
			PSP_Wait(porth,Req);
		    }
		}
	    }
	    for (j=0;j<np;j++){
		if ((mapnode[j]>=0)  ){
		    head.xdata.rank = j;
		    head.xdata.node = mapnode[j];
		    head.xdata.port = mapport[j];
		    for (k=0;k<arg_cnt;k++){
			Req = PSP_ISend(porth,0,0,&head.header,sizeof(struct xdata_T),mapnode[r],mapport[r]);
			PSP_Wait(porth,Req);
		    }
		}
	    }
	    break;
	}
	case 2:{
	    int r=head.xdata.rank;
	    mapnode[r] = head.xdata.node;
	    mapport[r] = head.xdata.port;
	    head.xdata.type=3;
	    head.xdata.from=rank;
	    head.xdata.to  =r;
	    Req = PSP_ISend(porth,0,0,&head.header,sizeof(struct xdata_T),mapnode[r],mapport[r]);
	    PSP_Wait(porth,Req);
	    break;
	}
	case 3:{
	    head.xdata.type=4;
	    Req = PSP_ISend(porth,0,0,&head.header,sizeof(struct xdata_T),mapnode[0],mapport[0]);
	    PSP_Wait(porth,Req);
	    break;
	}
	case 4:{
	    conrecv[head.xdata.from][head.xdata.to]+=1;
	    break;
	}
	default:{
	    /* never be here */
	    fprintf(out,"recv type %d\n",head.xdata.type);
	}
	}
	finish=1;
	for (j=1;j<arg_np;j++){
	    for (k=1;k<arg_np;k++){
		if (conrecv[j][k] <arg_cnt){
		    finish=0;
		    break;
		}
	    }
	}
	
    }
    signal(SIGALRM,SIG_IGN);
    time_handler(0);
    fprintf(out,"All connections ok\n",head.xdata.type);
    fclose(out);
}





int main(int argc, char **argv)
{
    struct psm_mcpif_mmap_struct * ms;
    int pshal_fd=0;
    int res;
    int i;
    int size;
    char mes[64];

    signal(SIGINT , cleanup);
    //printf(__DATE__" "__TIME__"\n");

    if (arg_parse(argc, argv,
		  "", "Usage: %s [options]", argv[0],
		  "-np %d",&arg_np," ",
		  "-cnt %d",&arg_cnt," ",
		  0) < 0){
        exit(1);
    }
    pshal_default_mcp=NULL;
    PSHALStartUp(0);

    if (arg_np <= 0) {
	fprintf(stderr,"missing arg -np\n");
	exit(1);
    }

    run(argc,argv,arg_np);
    return 0;
}




/*
 * Local Variables:
 *  compile-command: "make test_nodes"
 *
 */

/*
ssh io "cdl psm;cd tools;make test_nodes";cdl psm;scp -C tools/alpha_Linux/test_nodes alice:
*/


























