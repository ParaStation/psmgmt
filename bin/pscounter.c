
/* 2001-09-04 (c) ParTec AG, Jens Hauke */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "ps_types.h"
#include "pshal.h"




int main(int argc, char **argv)
{
    PSHALInfoCounter_t *ic;
    PSHALInfoCounter_t ic2;
    int i;
    int loop;
    int time=0;
    int cont=0;
    
    if (PSHALStartUp(0)){
	perror("PSHALStartUp");
	exit(1);
    }

    if (argc>1){
	time=atoi(argv[1]);
	printf("Counter / %d seconds\n",time);
	cont=1;
	if (!time){
	    printf("Use: %s [delay [count]]\n",argv[0]);
	    exit(1);
	}
    }
    if (argc>2){
	cont=atoi(argv[2]);
    }


    for (loop=0;loop<=cont;loop++){
	ic=PSHALSYSGetInfoCounter();
	if ( !ic ){
	    perror("GetInfoCounter");
	    exit(1);
	}
	if (loop==0){
	    /* Print Header */
	    for (i=0;i<ic->n;i++){
		printf("%8s ",ic->counter[i].name);
	    }
	    printf("\n");
	}
	if (!time){
	    /* Print Values */
	    for (i=0;i<ic->n;i++){
		char ch[10];
		/* calc column size from name length */
		sprintf(ch,"%%%du ",MAX(strlen(ic->counter[i].name),8));
		printf(ch,ic->counter[i].value);
	    }
	    printf("\n");
	}else{
	    if (loop>0){
		/* Print Delta Values */
		for (i=0;i<ic->n;i++){
		    char ch[10];
		    /* calc column size from name length */
		    sprintf(ch,"%%%du ",MAX(strlen(ic->counter[i].name),8));
		    printf(ch,ic->counter[i].value-ic2.counter[i].value);
		}
		printf("\n");
	    }
	}
	if (time){
	    sleep(time);
	    ic2= *ic;
	}
    }
    return 0;
}






