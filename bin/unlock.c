
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

#define PSM_DEVICE   "/dev/psm"

int main(int argc, char **argv){
    static int pshal_fd=0;   
    
    if((pshal_fd = open(PSM_DEVICE,O_RDWR))==-1){  
	perror("Unable to open " PSM_DEVICE);
	exit(-1);
    }
    if (ioctl(pshal_fd, PSHAL_UNLOCKMODULE)!=0){
	perror("Unable to call PSHAL_UNLOCKMODULE");
    }
    
    printf("OK?\n");
    return 0;
}


/*
 * /remove me/Local Variables:
 *  compile-command: "gcc x.c -o x -save-temps &&x"
 *
 *
 */




