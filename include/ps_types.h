/***********************************************************
 *                  ParaStation3
 *
 *       Copyright (c) 2000 ParTec AG Karlsruhe
 *       All rights reserved.
 ***********************************************************/
/**
 * ps_types:
 *
 * $Id: ps_types.h,v 1.3 2002/05/24 10:22:55 hauke Exp $
 *
 * @author  
 *         Jens Hauke <hauke@par-tec.de>
 *
 * @file
 ***********************************************************/

#ifndef _PSM_TYPES_H_
#define _PSM_TYPES_H_

#if   defined( __alpha )
#  define ARCH64BIT
#elif defined( __i386 )
#  define ARCH32BIT
#elif defined( __arch64__ )
#  define ARCH64BIT
#elif defined( __lanai__ )
#  define ARCH32BIT
#else
#error Unknown architecture
#endif


#define MIN(a,b)      (((a)<(b))?(a):(b))
#define MAX(a,b)      (((a)>(b))?(a):(b))

#define INT8	char
#define INT16	short
#define INT32	int
#ifdef ARCH64BIT
#define INT64	long
#else
#define INT64	long long
#endif

#define UINT8	unsigned char
#define UINT16	unsigned short
#define UINT32	unsigned int
#ifdef ARCH64BIT
#define UINT64	unsigned long
#else
#define UINT64	unsigned long long
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

#ifndef BIT
#define BIT(n)          ((unsigned int) 1 << n)
#endif


/* Align var to a 4,8 or 16 byte boundary */
#if defined( __GNUC__ )

#define ALIGN( align , var )  var __attribute__ ((aligned (align)))
#define ALIGN4( var ) ALIGN(4,var)
#define ALIGN8( var ) ALIGN(8,var)
#define ALIGN16( var ) ALIGN(16,var)

#define TALIGN( type, align , var )  type var __attribute__ ((aligned (align)))
#define TALIGN4( type, var )  type ALIGN(4,var)
#define TALIGN8( type, var )  type ALIGN(8,var)
#define TALIGN16( type, var ) type ALIGN(16,var)

#elif defined( __DECC )

#define ALIGN( align_ , var )  _USE_TALIGN_INSTEAD_OF_ALIGN_
#define ALIGN4( var ) ALIGN(4,var)
#define ALIGN8( var ) ALIGN(8,var)
#define ALIGN16( var ) ALIGN(16,var)

/* DECC has no align pragma for types, only for variables.*/
#define TALIGN( type, align , var )  USE TALIGN8
#define TALIGN4( type, var )  int  _align_fill4_[];type var
#define TALIGN8( type, var )  long _align_fill8_[];type var
#define TALIGN16( type, var ) WANT WORK

#else

#ifndef NOALIGNWARN
#warning No ALIGN Macros
#endif

#endif




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


#if defined( __GNUC__ )
/* Named initializers in structures */
#define sinit( name ) name :
/* Used for arrays with size zero */
#define zeroarray 0
#else
#define sinit( name )
#define zeroarray 
#endif

#endif /* _PSM_TYPES_H_*/




