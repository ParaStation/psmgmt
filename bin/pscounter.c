
/* 2001-09-04 (c) ParTec AG, Jens Hauke */

#include <stdio.h>
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
    int i;
    
    if (PSHALStartUp(0)){
	perror("PSHALStartUp");
	exit(1);
    }

    ic=PSHALSYSGetInfoCounter();
    if ( !ic ){
	perror("GetInfoCounter");
	exit(1);
    }

    /* Print Header */
    for (i=0;i<ic->n;i++){
	printf("%8s ",ic->counter[i].name);
    }
    printf("\n");

    /* Print Values */
    for (i=0;i<ic->n;i++){
	char ch[10];
	/* calc column size from name length */
	sprintf(ch,"%%%du ",MAX(strlen(ic->counter[i].name),8));
	printf(ch,ic->counter[i].value);
    }
    printf("\n");

    return 0;
}






