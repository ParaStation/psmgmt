
#define UINT32 unsigned int
#define UINT8  unsigned char

#include <string.h>
#include <unistd.h>
#include "stdio.h"
#include "license.h"
#include "../include/dump.c"








int main(int argc, char **argv)
{
    char buf[400];
    char buf2[400];
    char orig[400];
    int len;
    int i;

//      char enc[256];
    
//      int i;
//      for (i=0;i<256;i++){
//    	enc[ i ]=0;
//      }
//      for (i=0;i<64;i++){
//    	enc[ LICBITPACK_B2A(i)]=i;
//      }
//      for (i=0;i<128;i++){
//    	printf("\\x%02x",enc[i]);
//      }
//      printf("\n");
//      for (i=0;i<128;i++){
//    	printf("%c",isprint(i)?i:'.');
//      }
//      printf("\n");
//      exit(0);


//      for (i=0;i<64;i++){
//  	printf("%4d %4d - %4d\n",i,LICBITPACK_A2B(LICBITPACK_B2A(i)),LICBITPACK_B2A(i));
//      }
    
    
    len=read(0,buf,atoi(argv[1]))-1;
//    len = 40;
//    strncpy(buf,"sadfgsdfgosgowjerogjweoijgwerg",len);
    memcpy(orig,buf,100);
    fprintf(stderr,"len %d\n",len);
//    write(1,buf,len);
//    dump(buf,0,len,0,16,"orig");
    pslic_encode( buf,len,0x51451233);

    pslic_bin2ascii(buf,len,buf2);

    printf("\n[");fflush(stdout);
    write(1,buf2,pslic_blen2alen(len));
    printf("]\n");

    memset(buf,0,100);

    pslic_ascii2bin(buf2,pslic_blen2alen(len),buf);
    
    pslic_decode( buf,len,0x51451233);
//    dump(buf,0,len,0,16,"dec");
    
//    write(1,buf,len);
    printf("\n");
    if (memcmp(buf,orig,len)){
	printf("Falsch\n");
	dump(buf,0,len,0,16,"buf");
	dump(orig,0,len,0,16,"orig");
    }else
  	printf("OK\n");
    
	   
    return 0;
}


/*
 * /remove me/Local Variables:
 *  compile-command: "gcc x.c -o x -save-temps &&x"
 * End:
 *
 */
