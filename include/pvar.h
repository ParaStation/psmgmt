
/*********************************************************************/
/* 2000 Jens Hauke 						     */
/*	hauke@wtal.de						     */
/*********************************************************************/
/* Print content and structure of variable
 * 2000-09-27	Initial implementation
 * For basic use:
 *   compile with:  -g -lbfd -liberty 
 *   call PVarInit(argv[0]);   
 *   pvar(): To print content and structure of variable
 *   pdump(): to print a hexdump
 * Advance:
 *   PVarRegisterPrint()  : Define alternative print functions
 *   PVarUnregisterPrint()
 *   PVarIndent()
 **********************************************************************/




#ifndef _PVAR_H_
#define _PVAR_H_


#define N_STABSFILE 512      /* max files */
#define N_STABSREF  (1*1024) /* max types per file */
#define N_STABSALL  8*1024   /* max types */
#define N_MAXTYPELEN 50      /* max size for typenames */

//#define PVAR_PRINT_WARNINGS
#ifdef __cplusplus
    extern "C" {
#endif	


typedef struct stabsstr_t{
    char typechar;
    char type;
    int fieldcnt;
    char name[N_MAXTYPELEN];
    int arraydim;
    int size;
    struct stab_print * print;
    struct StabsField_t{
	char type;
	int file;
	int ref;
	int bitfrom;
	int bitto;
	char * name;
    } field[0];
}stabsstr_T;




/* Hexdump of addr: */
extern void pdump( void * addr, int offset, int len,int allign,
		  int cols,char * desc );
/* Structure dump of var at address data.
 * Use Type typename (eg "long int")
 * Print arrays until index PVarMaxArrayLen
 * Follow Pointer recursion times                  */
extern void pvar( void * data, char * Descript,char * type_name,
		  int recursion);

#define PVAR( var , recursion) pvar(&var,#var,#var,recursion)

/* Call this first for Initialization
 * filename is the argv[0], if compiled with -g or
 * an objectfile with stab entrys. */
extern void PVarInit(char* filename);


struct stab_print{
    char type_name[N_MAXTYPELEN];	/* print work on this type */
    int xparam;				/* extra parameter for you */
    /* Callback function to print one element of type typename*/
    int (*print) ( int xparam,struct stabsstr_t * ss,
		   void * data,int recursion,int indent);
    /* Callback function to print size elements of type typename */
    int (*printarray) ( int xparam,struct stabsstr_t * ss,void * data,int size,
			int recursion,int indent);
    struct stab_print * next;		/* reserved */
};


/* Size in Bytes of type typename */
extern int pvarsize( char * type_name);

extern void PVarRegisterPrint(struct stab_print * sp);
extern void PVarUnregisterPrint(struct stab_print * sp);
extern void PVarIndent(int i);

// default print function for array of char (eg char xyz[100])
extern int stab_print_char_arraybase( int xparam,struct stabsstr_t * ss,
				      void * data,int size,
				      int recursion,int indent);
// default print function for basetypes (int,double...)    
extern int stab_print_base_print(int xparam, struct stabsstr_t * ss,void * data,
				 int recursion,int indent);
extern int PVarMaxArrayLen; // Max Len for Arrays
extern int PVarMaxCharLen;  // Max Len for array of char 
extern int PVarSwitchEndian;// For printing integers with other byteorder.
#ifdef __cplusplus
    }
#endif



#endif


/*
 *  Local Variables:
 *  compile-command: "gcc  pvar.c -DSTAB_STANDALONE -lbfd -liberty -o pvar -Wall -g &&pvar"
 *
 *
 */

