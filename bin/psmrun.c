
/*
 * 2001-11-05 ParTec AG , Jens Hauke
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>


#include <pse.h>
//#include <psi.h>
char * PSI_LookupInstalldir(void);
#include <pshal.h>

#define PSM_CONFIG_FILE "psm.config"


int debug=0;

#define DPRINT(fmt,rest...) do{			\
    if (debug){					\
	fprintf(stderr,fmt ,##rest);		\
    }						\
}while(0);

extern char **environ;


char *GetConfigFileName(void)
{
    char *idir=NULL;
    static char *cfile=NULL;
    if (!cfile){
	cfile= getenv("PSMCONFIG");
	if (!cfile){
	    DPRINT("Installdir:\n");
	    idir=PSI_LookupInstalldir();
	    DPRINT("'%s'\n",idir);
	    cfile = malloc(strlen(idir)+strlen("/config/psm.config")+4);
	    strcpy(cfile,idir);
	    strcat(cfile,"/config/psm.config");
	    DPRINT("Using configuration file '%s'\n",cfile);
	}
    }
    return cfile;
}


char *getline(FILE *fd,char *str,int n)
{
    return fgets(str,n,fd);
}



int GetNoOfNodes(char *FileName)
{
    FILE *fd;
    int NoOfNodes=-1;
    char str[1000];
    char st1[40];
    int val;
    int ret;
    fd = fopen(FileName,"r");

    if (!fd){
	fprintf(stderr,"Cant open file %s:%s\n",FileName,strerror(errno));
	exit(1);
    }

    do{
	if (!getline(fd,str,sizeof(str)))
	    goto escape;
	ret=sscanf(str,"%30s%d\n",st1,&val);
//  	if (ret >1){
//  	    DPRINT("conf read:%s:%d:%d:\n",st1,val,ret);
//  	}
	if ((ret ==2) && !strcasecmp(st1,"NrOfNodes")){
	    NoOfNodes=val;
	    goto escape;
	}
    }while(NoOfNodes < 0);
	
 escape:	
    fclose(fd);
    return NoOfNodes;
}

char *GetNodeName(char *FileName,int No)
{
    FILE* fd;
    int NoOfNodes=-1;
    char str[1000];
    static char st1[40];
    int val;
    int ret;
    char *NodeName=0;

    fd = fopen(FileName,"r");
    if (!fd){
	fprintf(stderr,"Cant open file %s:%s\n",FileName,strerror(errno));
	exit(1);
    }

    do{
	if (!getline(fd,str,sizeof(str)))
	    goto escape;
	ret=sscanf(str,"%30s%d\n",st1,&val);
//  	if (ret>1){
//  	    DPRINT("conf read:%s,%d,%d\n",st1,val,ret);
//  	}
	if ((ret ==2) && (val==No)){
	    if (st1[0]!='#'){
		NodeName=st1;
		goto escape;
	    }
	}
    }while(NoOfNodes < 0);
	
 escape:	
    fclose(fd);
    return NodeName;
}


char *getcwd_alloc(void)
{
    int size=1000;
    char *buf=NULL;
    char *ret;

    do{
	if (buf) free(buf);
	buf=malloc(size);
	ret=getcwd(buf,size);
	size+=1000;
    }while (!ret && (errno==ERANGE));
    return ret;
}

/*
 * Allocate a new string with malloc with the fullpath from path
 */
char *FullPath(char *path)
{
    if (path[0]=='/'){
	return strdup(path);
    }else{
	char *cwd=getcwd_alloc();
	char *newpath=malloc(strlen(path)+strlen(cwd)+4);
	
	strcpy(newpath,cwd);
	strcat(newpath,"/");
	strcat(newpath,path);

	free(cwd);cwd=NULL;

	/*
	 * remove automount directory name.
	 */
	if(strncmp(newpath,"/tmp_mnt",strlen("/tmp_mnt"))==0){
	    memmove(newpath,newpath+strlen("/tmp_mnt"),strlen(newpath));
	}else if(strncmp(newpath,"/export",strlen("/export"))==0){
	    memmove(newpath,newpath+strlen("/export"),strlen(newpath));
	}
	return newpath;
    }
}

/*
 * Allocate a new string with malloc with the current directory
 */
char *FullCwd(void)
{
    char *newpath=getcwd_alloc();

    /*
     * remove automount directory name.
     */
    if(strncmp(newpath,"/tmp_mnt",strlen("/tmp_mnt"))==0){
	memmove(newpath,newpath+strlen("/tmp_mnt"),strlen(newpath));
    }else if(strncmp(newpath,"/export",strlen("/export"))==0){
	memmove(newpath,newpath+strlen("/export"),strlen(newpath));
    }
    return newpath;
}


void PrintFreeNode(int argc,char **argv)
{
    int masternode =0;
    int masterport =0;
    int rank=0;
    setenv("PSI_NOMSGLOGGERDONE","1",1);
    PSEinit(2,argc,argv,&masternode,&masterport,&rank);
    if (rank){
	PSHALStartUp(0);
	printf("--startnode %d\n",PSHALSYSGetID());
    }
    sleep(1);
    PSEfinalize();
}


#define psps_alen2blen(alen) (((alen) / 2))
#define psps_blen2alen(blen)    ((blen) * 2)



static inline void
psps_ascii2bin(unsigned char *ascii,int alen,unsigned char *bin ){
    while (alen--){
	*bin= (((*ascii)-'a')<<4) +  (((*(ascii+1))-'a'));
	bin++;
	ascii+=2;
    };
}

static inline void
psps_bin2ascii(unsigned char *bin,int blen,unsigned char *ascii ){
    while (blen--){
	*ascii=     'a' + ((*bin) >>  4);
	*(ascii+1)= 'a' + ((*bin) & 0xf);
	bin++;
	ascii+=2;
    }
}



static
char *arg_encode(int argc,char **argv)
{
    int len;
    int i;
    char *bin;
    char *asc;
    char *a;
    len=1;
    for (i=0;i<argc;i++){
	len=len+strlen(argv[i])+1;
    }

    bin=malloc(len);
    asc=malloc(psps_blen2alen(len))+1;

    a=bin;
    DPRINT("Encode: ");
    for (i=0;i<argc;i++){
	memcpy(a,argv[i],strlen(argv[i])+1);
	a+=strlen(argv[i])+1;
	DPRINT("%s ",argv[i]);
    }
    *a++=0;
    psps_bin2ascii(bin,len,asc);
    asc[ psps_blen2alen(len)]=0;
    DPRINT("\n-> %s\n",asc);
    free(bin);
    return asc;
}

static
void arg_decode(char *asc,int *eargc,char ***eargv)
{
    char *bin;
    char *a;
    int i;
    DPRINT("asc: %s\n",asc);
    bin = malloc(psps_alen2blen(strlen(asc)));
    psps_ascii2bin(asc,strlen(asc),bin);
    a=bin;
    *eargc=0;
    while (*a++){
	(*eargc)++;
	while (*a++){};
    }
    *eargv=(char **)malloc(sizeof(char **)* (*eargc + 1));
    DPRINT("argc: %d\n",*eargc);
    a=bin;
    i=0;
    while (*a++){
	(*eargv)[i++] = a-1;
	while (*a++){};
    }
    (*eargv)[i++]=0;

}

int cpid;
//#define DEBUG_OUT
#ifdef DEBUG_OUT
FILE *out=NULL;
#endif

void sig_child(int sig)
{
    int status;
#ifdef DEBUG_OUT
    if (out){
	fprintf(out,"Recv Signal %d\n",sig);
	fflush(out);
    }
#endif
    wait(&status);
#ifdef DEBUG_OUT
    if (out){
	fprintf(out,"exit with %d\n",WEXITSTATUS(status));
	fflush(out);
    }
#endif
    exit(WEXITSTATUS(status));
}

void start_client(int argc,char **argv)
{
    int eargc;
    char **eargv;
    int i;

    DPRINT("cd %s\n",argv[0]);
    if (chdir( argv[0] ))
	perror("chdir");
    /*parastation expect correct PWD */
    setenv("PWD",argv[0],1);
    arg_decode( argv[1],&eargc,&eargv );

    DPRINT("Call:\n");
    for(i=0;eargv[i];i++){
	DPRINT("%s ",eargv[i]);
    }
    DPRINT("\n");

    if (! isatty(STDERR_FILENO)){
	signal(SIGCHLD,sig_child);
	cpid = fork();
    }else{
	/* with tty, we dont need a monitor thread */
	cpid=0;
    }
    if (!cpid){
	/* working thread */
	execvp(eargv[0],eargv);
	
	fprintf(stderr,"Call:\n");
	for(i=0;eargv[i];i++){
	    fprintf(stderr,"%s ",eargv[i]);
	}
	fprintf(stderr,"\nfailed!\n");
	exit(1);
    }else{
	/* monitor thread */
	char buf[1];
	int rs;
	int status;
#ifdef DEBUG_OUT
	out=fopen("/home/hauke/out","w");
	if (!out){
	    perror("open out");
	}
	fprintf(out,"wait for read\n");
	fflush(out);
#endif
#if 0 /* wont work */
	do{
	    /* timed flush of stdout */
	    int ret;
	    struct timeval to;
	    fd_set fds_read;
	    to.tv_sec=3;
	    to.tv_usec=0;
	    FD_ZERO( &fds_read);
	    FD_SET(STDERR_FILENO,&fds_read);
	    ret=select(STDERR_FILENO+1,&fds_read,NULL,NULL,&to);
	    printf("select ret=%d read=%d \n",
		    ret,
		   FD_ISSET(STDERR_FILENO,&fds_read));
	    fflush(stdout);
	    if (FD_ISSET(STDERR_FILENO,&fds_read))
		break;
	}while(1);
#endif 
	rs=read(STDERR_FILENO,buf,sizeof(buf));
#ifdef DEBUG_OUT
	fprintf(out,"read %d byte : %d kill in 10 sec\n",rs,buf[0]);
	fflush(out);
	sleep(10);
#endif
	kill(cpid,SIGTERM);
	wait(&status);
#ifdef DEBUG_OUT
	fprintf(out,"kill %d\n",cpid);
	fflush(out);
#endif
	exit(WEXITSTATUS(status));
    }
}

int main(int argc,char **argv)
{
    char *ConfigFile;
    int NrOfNodes=-1;
    char *MonitorNode;
    int  MonitorNodeNo=-1;
    int MonitorPipe[2];
    char *StartNode;
    int StartNodeNo=-1;


    if (getenv("DEBUG")){
	debug=1;
	DPRINT("DEBUG is on\n");
    }
    srandom(getpid());

    if (argc == 1){
	fprintf(stderr,"Use:\n%s programm [args...]\n",argv[0]);
	exit(1);
    }
    
    { /* Check Executable location */
	FILE *tf;
	tf = fopen(argv[0],"r");
	if (tf && (argv[0][0] == '/')){
	    DPRINT("%s ok.\n",argv[0]);
	    fclose(tf);
	    /* all ok */
	}else{
	    char *buf=malloc(2000);/*Arggg!*/
	    int ret;
	    DPRINT("Read /proc/self/exe ");
	    ret = readlink("/proc/self/exe",buf,2000 );
	    if (ret >0){
		buf[ret]=0;
		argv[0]=buf;
	    }
	    DPRINT("-> using %s\n",argv[0]);
	}
	
    }

    if ((argc == 2)&& (!strcmp(argv[1],"--startnode"))){
	PrintFreeNode(argc,argv);
	exit(0);
    }

    if ((argc >= 2)&& (!strcmp(argv[1],"--start"))){
	start_client(argc-2,&argv[2]);
	exit(0);
    }

    ConfigFile=GetConfigFileName();
    if (!ConfigFile){
	fprintf(stderr,"Cant locate " PSM_CONFIG_FILE "\n");
	exit(1);
    }

    NrOfNodes=GetNoOfNodes(ConfigFile);
    if (NrOfNodes<=0){
	fprintf(stderr,"Cant get NrOfNodes!\n");
	exit(1);
    }
    while (MonitorNodeNo <0){
	MonitorNodeNo = random() % NrOfNodes;
    }
    MonitorNode=GetNodeName(ConfigFile,MonitorNodeNo);
    if (!MonitorNode){
	fprintf(stderr,"GetNodeName(%d) failed!\n",MonitorNodeNo);
	exit(1);
    }
    DPRINT("Ask node '%s'(ID %d) for a free node\n",MonitorNode,MonitorNodeNo);

    pipe(MonitorPipe);

    /*
     * search free node
     */
    if (!fork()){
	char *eargv[5];

	eargv[0]=strdup("ssh");
	eargv[1]=strdup(MonitorNode);
	eargv[2]=strdup(argv[0]);
	eargv[3]=strdup("--startnode");
	eargv[4]=0;
	close(STDIN_FILENO);
	dup2(MonitorPipe[1], STDOUT_FILENO);
	execvp("ssh",eargv);
	
	fprintf(stderr,"Cant start remote process1!");
	perror("");
	exit(1);
    }
    close(MonitorPipe[1]); // Close the writing end to detect broken pipe

    /*
     * read magic
     */
    {
	int ret;
	char magic[1000]="";
	FILE *fd=fdopen (MonitorPipe[0], "r");
	do{
	    ret=fscanf(fd,"%s %d",magic,&StartNodeNo);
	    if (ret == EOF){
		fprintf(stderr,"ssh %s %s --startnode\nfailed!\n",MonitorNode,argv[0]);
		exit(1);
	    }
//	    DPRINT("Read %s\n",magic);
	}while(strcmp(magic,"--startnode"));
    }

    StartNode=GetNodeName(ConfigFile,StartNodeNo);
    if (!StartNode){
	fprintf(stderr,"Cant get name for node %d!\n",StartNodeNo);
	exit(1);
    }
    DPRINT("Start application from node '%s'(ID %d)\n",
	   StartNode,
	   StartNodeNo);

    {
	// Move arguments behind programname
	int ppos=1;
	int i;
	while (ppos < argc){
	    if       (!strcmp("-np",argv[ppos])){
		ppos+=2;
	    }else{
		break;
	    }
	}
	if (ppos<argc){
	    char *tmp= argv[ppos];
	    for (i=ppos;i>1;i--){
		argv[i]=argv[i-1];
	    }
	    argv[1]=tmp;
	}
    }
    
    {
	/*
	 * Start the program
	 */
	char **eeargv;
	int i;
	int n;
	eeargv=(char**)malloc(sizeof(char **)*(8));
	n=0;
	eeargv[n++]=strdup("ssh");
	DPRINT("Check tty.");
	if ((isatty(STDIN_FILENO))&&(isatty(STDOUT_FILENO))){
	    DPRINT("tty found.\n");
	    eeargv[n++]=strdup("-t");
	}else{
	    DPRINT("No tty found.\n");
	}
	eeargv[n++]=strdup(StartNode);
	eeargv[n++]=strdup(argv[0]);
	eeargv[n++]=strdup("--start");
	eeargv[n++]=FullCwd();
	eeargv[n++]=arg_encode(argc - 1,&argv[1]);
	eeargv[n++]=0;

	for(i=0;eeargv[i];i++){
	    DPRINT("%s ",eeargv[i]);
	}
	DPRINT("\n");

	execvp("ssh",eeargv);

	fprintf(stderr,"Call:\n");
	for(i=0;eeargv[i];i++){
	    fprintf(stderr,"%s ",eeargv[i]);
	}
	fprintf(stderr,"\nfailed!\n");
	exit(1);
    }
    return(0);

}
