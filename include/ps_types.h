
/*************************************************************fdb*
 * define machine independent types 
 * (UINT32,INT8 ...)		      
 *************************************************************fde*/


#ifndef _PSM_TYPES_H_
#define _PSM_TYPES_H_

#ifdef __alpha
# ifndef ARCH64BIT
#  define ARCH64BIT
# endif
#else
# ifndef ARCH32BIT
#  define ARCH32BIT
# endif
#endif


#define MIN(a,b)      (((a)<(b))?(a):(b))
#define MAX(a,b)      (((a)>(b))?(a):(b))

#define INT8	char
#define INT16	short
#define INT32	int
#ifdef ARCH64BIT
#define INT64	long
#elif defined(ARCH32BIT)
#define INT64	long long
#else
@echo "Unknown Architecture"
#endif

#define UINT8	unsigned char
#define UINT16	unsigned short
#define UINT32	unsigned int
#ifdef ARCH64BIT
#define UINT64	unsigned long
#elif defined(ARCH32BIT)
#define UINT64	unsigned long long
#else
@echo "Unknown Architecture"
#endif

#ifdef __lanai__
#define MCP_POINTER(t)  t *
#else
typedef UINT32 MCPPointer_t;
#define MCP_POINTER(t)  MCPPointer_t
#endif

#ifndef NULL
#define NULL 0
#endif


//  #ifdef ARCH64BIT
//  #define MCP_POINTER(t)	int
//  #elif defined(ARCH32BIT)
//  #define MCP_POINTER(t)  t *
//  #else
//  @echo "Unknown Architecture"
//  #endif

#ifndef BIT
#define BIT(n)          ((unsigned int) 1 << n)
#endif


/* Align var to a 4,8 or 16 byte boundary */

#define ALIGN( align , var )  var __attribute__ ((aligned (align)))
#define ALIGN4( var ) ALIGN(4,var)
#define ALIGN8( var ) ALIGN(8,var)
#define ALIGN16( var ) ALIGN(16,var)

#define ALIGNPAGE( var ) ALIGN(MAX_HOST_PAGESIZE,var)

#define NBIT1		0
#define NBIT2		1
#define NBIT4		2
#define NBIT8		3
#define NBIT16		4
#define NBIT32		5
#define NBIT64		6
#define NBIT128		7
#define NBIT256		8
#define NBIT512		9
#define NBIT1024	10
#define NBIT2048	11
#define NBIT4096	12
#define NBIT8192	13
#define NBIT16384	14
#define NBIT32768	15
#define NBIT65536	16

#define NBIT(x)		_NBIT(x)
#define _NBIT(x)	NBIT##x


/* mainly used for pagealignement : */
#define ROUND_UP(align, val)  ( ((val) + ((align)-1)) & (~((align)-1)))
#define ROUND_DOWN(align, val)  ((val) & (~((align)-1)))


// Decode val in Bits from-(from+len-1) (use len bits)
#define BITDEC(from,len,val) (((val) >> (from)) &((1<<(len))-1))
// Encode val in Bits from-(from+len-1) (use len bits)
#define BITENC(from,len,val) (((val) & ((1<<(len))-1))<<(from))

#define BITENC1( l1,v1 )					\
(    BITENC( 0			, (l1)		, (v1)))

#define BITENC2( l1,v1,l2,v2 )					\
(    BITENC( 0			, (l1)		, (v1))|	\
     BITENC( (l1)		, (l2)		, (v2)))

#define BITENC3( l1,v1,l2,v2,l3,v3 )				\
(    BITENC( 0			, (l1)		, (v1))|	\
     BITENC( (l1)		, (l2)		, (v2))|	\
     BITENC( (l1)+(l2)		, (l3)		, (v3)))

#define BITENC4( l1,v1,l2,v2,l3,v3,l4,v4 )			\
(    BITENC( 0			, (l1)		, (v1))|	\
     BITENC( (l1)		, (l2)		, (v2))|	\
     BITENC( (l1)+(l2)		, (l3)		, (v3))|	\
     BITENC( (l1)+(l2)+(l3)	, (l4)		, (v4)))

#define BITENC5( l1,v1,l2,v2,l3,v3,l4,v4,l5,v5 )		\
(    BITENC( 0			, (l1)		, (v1))|	\
     BITENC( (l1)		, (l2)		, (v2))|	\
     BITENC( (l1)+(l2)		, (l3)		, (v3))|	\
     BITENC( (l1)+(l2)+(l3)	, (l4)		, (v4))|	\
     BITENC( (l1)+(l2)+(l3)+(l4), (l5)		, (v5)))

#define BITDEC1(val, l1,r1)					\
{  r1=BITDEC( 0			, (l1)		, (val)); }

#define BITDEC2(val, l1,r1,l2,r2)				\
{  r1=BITDEC( 0			, (l1)		, (val));	\
   r2=BITDEC( (l1)		, (l2)		, (val)); }

#define BITDEC3(val, l1,r1,l2,r2,l3,r3)				\
{  r1=BITDEC( 0			, (l1)		, (val));	\
   r2=BITDEC( (l1)		, (l2)		, (val));	\
   r3=BITDEC( (l1)+(l2)		, (l3)		, (val)); }

#define BITDEC4(val, l1,r1,l2,r2,l3,r3,l4,r4)			\
{  r1=BITDEC( 0			, (l1)		, (val));	\
   r2=BITDEC( (l1)		, (l2)		, (val));	\
   r3=BITDEC( (l1)+(l2)		, (l3)		, (val));	\
   r4=BITDEC( (l1)+(l2)+(l3)	, (l4)		, (val)); }

#define BITDEC5(val, l1,r1,l2,r2,l3,r3,l4,r4,l5,r5 )		\
{  r1=BITDEC( 0			, (l1)		, (val));	\
   r2=BITDEC( (l1)		, (l2)		, (val));	\
   r3=BITDEC( (l1)+(l2)		, (l3)		, (val));	\
   r4=BITDEC( (l1)+(l2)+(l3)	, (l4)		, (val));	\
   r5=BITDEC( (l1)+(l2)+(l3)+(l4), (l5)		, (val)); }


	

/* sizeof of field in struct structname */
#define ssizeof( structname , fieldname )			\
sizeof( ((struct structname *)0)->fieldname)
/* fieldoffset offield in struct structname */
#define soffset(structname , fieldname)				\
((long int)&(((struct structname*)0)->fieldname))

#define sendoff(structname , fieldname)				\
(soffset(structname,fieldname) + ssizeof(structname,fieldname))


#endif /* _PSM_TYPES_H_*/




