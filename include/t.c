
#if 0
 file_c=$0
 file_o=`echo $file_c|sed 's/.c$//'`
 if [ $file_c = $file_o ];then file_o="a.out";fi
 if [ ! -f $file_o ] || [ "`find $file_o -newer $file_c`" = "" ];then
# Executable is older than source or dont exist 
   if gcc $file_c -o $file_o ;then 
    $file_o $@
   fi
  else
    $file_o $@
  fi
 exit
#endif

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <math.h>
#include "ps_types.h"


main(int argc, char **argv)
{
// process_args(argc, argv);
    printf("%d %d %d %d",NBIT(2048),NBIT(1),NBIT(64),NBIT(8));
 return 0;
}


/*
 * /remove me/Local Variables:
 *  compile-command: "gcc x.c -o x -save-temps &&x"
 *
 *
 */
