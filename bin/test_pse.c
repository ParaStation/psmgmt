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

int arg_np;
int arg_cnt;


void run(int argc,char **argv,int np)
{
    int mapnode;
    int mapport;
    int rank;
    char name[100];
    mapnode=0;
    mapport=0;
    rank =0;
    
    PSEinit(np,argc,argv,&mapnode,&mapport,&rank);
//    sleep(rank/30);
    gethostname(name,sizeof(name)-1);

    printf("node: %d port: %d rank: %d host:%s\n",mapnode,mapport,rank,name  );
    sleep(3);
    PSEfinalize();
}





int main(int argc, char **argv)
{

    //printf(__DATE__" "__TIME__"\n");

    if (arg_parse(argc, argv,
		  "", "Usage: %s [options]", argv[0],
		  "-np %d",&arg_np," ",
		  "-cnt %d",&arg_cnt," ",
		  0) < 0){
        exit(1);
    }

    if (arg_np <= 0) {
	fprintf(stderr,"missing arg -np\n");
	exit(1);
    }

    run(argc,argv,arg_np);
    return 0;
}


/*
 * Local Variables:
 *  compile-command: "make test_pse"
 * End
 */

/*
ssh io "cdl psm;cd tools;make test_nodes";cdl psm;scp -C tools/alpha_Linux/test_nodes alice:
*/
