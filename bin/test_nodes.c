/*
 * 2001-09-17  ParTec AG , Jens Hauke
 *
 */

#include <stdio.h>
#include <stdlib.h>
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
static int arg_map=0;
int conrecv[maxnp][maxnp];
int mapnode[maxnp];

int finish=0;

void time_handler_old(int signal)
{
    int j,k;
#ifndef __DECC
    fprintf(stdout,"\e[H");
    fprintf(stdout,"\e[2J");
#endif
    for (j=0;j<arg_np;j++){
	fprintf(stdout,"%3d(node %3d) ",j,mapnode[j]);
	if (j) for (k=1;k<arg_np;k++){
	    fprintf(stdout,"%1d ",conrecv[j][k]);
	}
	fprintf(stdout,"\n");
    }
//	fprintf(out,"cnt:  %d/%d\n",i,
//		end);
    fflush(stdout);
}

#include <stdlib.h>


void print_list(int *ilist,int size)
{
    int i;
    int first,last;
//    qsort(ilist,size,sizeof(int *));
    if (!size){
	fprintf(stdout,"none");
	return;
    }
    first=0;
    last=0;
    for (i=1;i<size;i++){
	if (ilist[i] == ilist[i-1] + 1){
	    last = i;
	}else{
	    last=i-1;
	    if (first==last){
		fprintf(stdout,"%d,",ilist[first]);
	    }else{
		fprintf(stdout,"%d-%d,",ilist[first],ilist[last]);
	    }
	    first=i;
	    last=i;
	}
    }
    if (first==last){
	fprintf(stdout,"%d",ilist[first]);
    }else{
	fprintf(stdout,"%d-%d",ilist[first],ilist[last]);
    }
}

int print_list_compare(const void *a, const void *b)
{
    int va = *(int*)a;
    int vb = *(int*)b;
    if (va > vb)
	return 1;
    else
	return -1;
}

void print_list_sort(int *ilist,int size)
{
    int *silist = malloc(size * sizeof(int));
    int sisize = 0;
    int i;
    if (!size) return;
    qsort(ilist,size,sizeof(int),print_list_compare);
    silist[sisize++] = ilist[ 0 ];
    for (i=1;i<size;i++){
	if (silist[sisize-1] != ilist[i]){
	    silist[sisize++] = ilist[ i ];
	}
    }
    print_list(silist,sisize);
}

int answer_equal(int i,int j)
{
    int k;
    for (k=1;k<arg_np;k++){
	
	if (((conrecv[i][k] > 0) ^
	     (conrecv[j][k] > 0)))
	    return 0;
    }
    return 1;
}

void time_handler(int signal)
{
    int i,j,k;
    int *checked=(int *)malloc(sizeof(int)*arg_np);
    int tmpsize;
    int *tmp=(int *)malloc(sizeof(int)*arg_np);
    int *tmphost=(int *)malloc(sizeof(int)*arg_np);
    int tmpsize2;
    int *tmp2=(int *)malloc(sizeof(int)*arg_np);
    int *tmphost2=(int *)malloc(sizeof(int)*arg_np);
    
    memset(checked,0,arg_np*sizeof(int));
    memset(tmp,0,arg_np*sizeof(int));
    memset(tmphost,0,arg_np*sizeof(int));
    memset(tmp2,0,arg_np*sizeof(int));
    memset(tmphost2,0,arg_np*sizeof(int));

//    fprintf(stdout,"\e[H");
//    fprintf(stdout,"\e[2J");
    fprintf(stdout,"---------------------------------------\n");
    fprintf(stdout,"Master node %d\n",mapnode[0]);

    /* No Answer:*/
    tmpsize=0;
    for (j=1;j<arg_np;j++){
	if (mapnode[j] < 0){
	    checked[j]=1;
	    tmp[tmpsize++]=j;
	}
    }
    if (tmpsize){
	fprintf(stdout,"Wait for answer from process: ");
	print_list(tmp,tmpsize);
	fprintf(stdout,"\n");
    }

    /* Answer: */
    for (j=0;j<arg_np;j++){
	tmpsize=0;
	if (checked[j]) continue;
	checked[j]=1;
	tmphost[tmpsize]=mapnode[j];
	tmp[tmpsize++]=j;

	/* Find equal lines */
	for (i=j+1;i<arg_np;i++){
	    if (!checked[i] && answer_equal(j,i)){
		tmphost[tmpsize]=mapnode[i];
		tmp[tmpsize++]=i;
		checked[i]=1;
	    }
	}

	/* to */
	tmpsize2=0;
	tmphost2[tmpsize2]=mapnode[0];
	tmp2[tmpsize2++]=0;
	for (i=1;i<arg_np;i++){
	    if (conrecv[j][i]>0){
		tmphost2[tmpsize2]=mapnode[i];
		tmp2[tmpsize2++]=i;
	    }
	}

	fprintf(stdout,"Process ");
	print_list(tmp,tmpsize);
	if (tmpsize2){
	    fprintf(stdout," to ");
	    print_list(tmp2,tmpsize2);
	    fprintf(stdout," ( node ");
	    print_list_sort(tmphost,tmpsize);
	    fprintf(stdout," to ");
	    print_list_sort(tmphost2,tmpsize2);
	    fprintf(stdout," ) OK\n");
	}else{
	    fprintf(stdout," waiting ( node ");
	    print_list_sort(tmphost,tmpsize);
	    fprintf(stdout,")\n");
	}
//	fprintf(stdout,"%3d(node %3d) ",j,mapnode[j]);
    }



//        for (j=0;j<arg_np;j++){
//    	fprintf(stdout,"%3d(node %3d) ",j,mapnode[j]);
//    	 for (k=0;k<arg_np;k++){
//    	    fprintf(stdout,"%1d ",conrecv[j][k]);
//    	}
//    	fprintf(stdout,"\n");
//        }

    fflush(stdout);
    free(checked);
    free(tmp);
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
//    PSP_PortH_t rawporth;
    PSP_RequestH_t Req;
    int mapport[np];
    int rank;
    int i,j,k,end;
    char filename[100];
    FILE *out;
    struct itimerval timer;

    PSE_init(np,&rank);
    
    if (rank == -1){
	/* I am the logger */
	/* Set default to none: */
	setenv("PSI_NODES_SORT","NONE",0);
	PSE_spawn(argc, argv, &mapnode[0], &mapport[0],rank);
	/* Never be here ! */
	exit(1);
    }
    
	
//    PSE_init(np,argc,argv,&mapnode[0],&mapport[0],&rank);
    /* Initialize Myrinet */

    if (PSP_Init()){
	perror("PSP_Init() failed!");
	exit(-1);
    }
    if (!(porth = PSP_OpenPort(arg_port))){
	perror("Cant bind port!");
	exit(-1);
    }
//      if (!(rawporth = PSP_OpenPort(arg_port))){
//  	fprintf(stderr,"rank %d ",rank);
//  	perror("Cant bind raw port!");
//      }

    for (i=0;i<np;i++){
	mapnode[i]=-1;
	mapport[i]=-1;
	for (j=0;j<np;j++){
	    conrecv[i][j]=0;
	}
    }
    
    mapport[rank] = PSP_GetPortNo(porth);
    mapnode[rank] = PSP_GetNodeID();

    if (rank==0){
	/* Master node: Set parameter from rank 0 */
	PSE_spawn(argc, argv, &mapnode[0], &mapport[0],rank);
    }else{
	/* Client node: Get parameter from rank 0 */
	PSE_spawn(argc, argv, &mapnode[0], &mapport[0],rank);
    }
    
    if (rank>0){
//	sprintf(filename,"out.%d",rank);
//	out=fopen(filename,"w");
	out = stdout;
    }else{
	out=stdout;
	timer.it_interval.tv_sec=0;
	timer.it_interval.tv_usec=1500*1000;
	timer.it_value.tv_sec=0;
	timer.it_value.tv_usec=1500*1000;
	if (arg_map){
	    signal(SIGALRM,time_handler_old);
	}else{
	    signal(SIGALRM,time_handler);
	}
//	alarm(1);
	setitimer(ITIMER_REAL,&timer,0);
//	sleep(1);
    }

//    fprintf(out,"master %d:%d my rank:%d\n",mapnode[0],mapport[0],rank);
//    fflush(out);
//    fprintf(out,"\e[2J");

    /* Send info to master */
    head.xdata.type=1;
    head.xdata.rank=rank;
    head.xdata.node=PSP_GetNodeID();
    head.xdata.port=PSP_GetPortNo(porth);
    Req = PSP_ISend(porth,0,0,&head.header,sizeof(struct xdata_T),mapnode[0],mapport[0],0);
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
			Req = PSP_ISend(porth,0,0,&head.header,sizeof(struct xdata_T),mapnode[j],mapport[j],0);
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
			Req = PSP_ISend(porth,0,0,&head.header,sizeof(struct xdata_T),mapnode[r],mapport[r],0);
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
	    Req = PSP_ISend(porth,0,0,&head.header,sizeof(struct xdata_T),mapnode[r],mapport[r],0);
	    PSP_Wait(porth,Req);
	    break;
	}
	case 3:{
	    head.xdata.type=4;
	    Req = PSP_ISend(porth,0,0,&head.header,sizeof(struct xdata_T),mapnode[0],mapport[0],0);
	    PSP_Wait(porth,Req);
	    break;
	}
	case 4:{
	    conrecv[head.xdata.from][head.xdata.to]+=1;
	    break;
	}
	case 5:{ /* Recv EXIT from master */
	    PSE_finalize();
	    exit(0);
	}
	default:{
	    /* never be here */
	    fprintf(out,"recv type %d\n",head.xdata.type);
	}
	}
	finish=1;
	for (j=0;j<arg_np;j++){
	    for (k=0;k<arg_np;k++){
		if (conrecv[j][k] <arg_cnt){
		    finish=0;
		    break;
		}
	    }
	}
	
    }


    /* Exit all clients: */
    for (j=0;j<np;j++){
	if ((mapnode[j]>=0)  ){
	    head.xdata.type = 5; /* EXIT */
	    head.xdata.rank = j;
	    head.xdata.node = mapnode[j];
	    head.xdata.port = mapport[j];
	    Req = PSP_ISend(porth,0,0,&head.header,sizeof(struct xdata_T),mapnode[j],mapport[j],0);
	    PSP_Wait(porth,Req);
	}
    }

    signal(SIGALRM,SIG_IGN);
    if (rank==0){
	if (arg_map){
	    time_handler_old(0);
	}else{
	    time_handler(0);
	}
    }
    fprintf(out,"All connections ok\n");
    fclose(out);

    PSE_finalize();
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
		  "-map",ARG_FLAG(&arg_map)," print map",
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
//    PSE_finalize();
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


























