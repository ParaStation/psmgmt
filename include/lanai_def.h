
#ifndef _LANAI_DEF_H_
#define _LANAI_DEF_H_

#ifdef lanai7
#include "lanai7_def.h" /* lanai description */
#endif
#ifdef lanai9
#include "lanai9_def.h" /* lanai description */
#define WAKE_INT_BIT WAKE0_INT_BIT
#endif


/*
 * DMA Control Block for LANai 7 ( s.o. lanai7_def.h struct DMA_BLOCK )
 */

#define DMA_NEXTMASK	0xfffffff8
#define DMA_H2N		1 /*DMA_E2L * host to lanai 1 */
#define DMA_N2H		0 /*DMA_L2E * lanai to host 0 */

typedef struct _DMA_CONTROL_BLOCK {
  volatile UINT32 next_with_flags; /* Pointer to next control block */
  UINT32	csum;	/* ones complement cksum of this block */
  UINT32	len;	/* byte count */
  UINT32	lar;	/* LANai address */
  UINT32	eah;	/* high PCI address -- unused for 32bit PCI */
  UINT32	eal;	/* low 32bit PCI address */
} DMA_CONTROL_BLOCK;

#define DMA_CHANNEL_0	0x0
#define DMA_CHANNEL_1	0x1
#define DMA_CHANNEL_2	0x2
#define DMA_CHANNEL_3	0x3
#define FIFO_BASE_0	0x4
#define FIFO_BASE_1	0x5
#define FIFO_BASE_2	0x6
#define FIFO_BASE_3	0x7

typedef struct _DMA_FIFO {
  unsigned int dma_channel_base[4];
  unsigned int fifo_base[4];
} DMA_FIFO;

typedef struct _FIFO_COUNTER {
  unsigned char limit;
  unsigned char count;
}FIFO_COUNTER ;

typedef struct _FIFO_ENTRY {
  UINT32    addr;
  UINT32    data;
}FIFO_ENTRY;





#endif /* _LANAI_DEF_H_ */
