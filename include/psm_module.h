
/*************************************************************fdb*
 * $Id: psm_module.h,v 1.3 2001/12/13 18:17:09 hauke Exp $
 * structures and prototypes of psm_module.c
 *
 *************************************************************fde*/

#ifndef _PSM_MODULE_H_
#define _PSM_MODULE_H_

#include "psm_osif.h"
#include "psm_const.h"

typedef unsigned long psm_context_t;


struct psm_mod {
    struct psm_pci_dev	* pci_dev;
    struct psm_lanai	* lanai;
    struct psm_lanaiif	* lanaiif;
    struct psm_mcp	* mcp;
    struct psm_mcpif	* mcpif;
};

struct psm_mmap_result{
    int ulen;           /* used length */
    unsigned long kaddr; /*  phys address for offset*/
    enum { mmap_pci , mmap_dma } type;
    enum { mmapf_write = 1,
	   mmapf_read  = 2 } allowed_flags;
    char * region_name; /* name for mapped region */
    int   bufno; /* no of this buffer (for debug print to suppress some lines)*/
};
    


struct psm_mcpif {
    int (*ioctl) ( int kern,psm_context_t, unsigned int cmd, unsigned long arg);
    int (*mmap)  ( psm_context_t context,int offset,int len,
		   struct psm_mmap_result * result );
    void (*irq)  ( unsigned int irq,void * ptr);
    int (*open)  ( psm_context_t );
    int (*close) ( psm_context_t );
    int (*init)  ( struct psm_mod * psm_mod ,unsigned long param );
    int (*cleanup) ( void );
};


extern int psm_node_id;

/*
 * Zero copy statistics
 */
#define PSM_ZC_SEND_OK			0
#define PSM_ZC_SEND_MLOCK_FAIL		1
#define PSM_ZC_SEND_VIRT2BUS_FAIL	2
#define PSM_ZC_SEND_DISABLED		3
#define PSM_ZC_RECV_OK			0
#define PSM_ZC_RECV_MLOCK_FAIL		1
#define PSM_ZC_RECV_VIRT2BUS_FAIL	2
#define PSM_ZC_RECV_DISABLED		3

#define PSM_ZC_SEND_INC(name)  psm_zc_send_cnt[PSM_ZC_SEND_##name]++
#define PSM_ZC_RECV_INC(name)  psm_zc_recv_cnt[PSM_ZC_SEND_##name]++
extern int psm_zc_send_cnt[4];
extern int psm_zc_recv_cnt[4];

/*
 * Enable/disable zero copy
 */
extern int zc;


int psm_ioctl(int kern,psm_context_t context, unsigned int cmd, unsigned long arg);
int psm_open( psm_context_t * context );
int psm_close( psm_context_t context );
extern int psm_mmap( psm_context_t context,int offset,int len,struct psm_mmap_result * result );



void psm_cleanup(void);
int psm_init(void);


extern inline int psm_copy_from_user_or_kernel(int kern,void *to, const void *from,
				 unsigned long n )
{
    if (!kern)
	return copy_from_user(to,from,n);
    else{
	memcpy(to,from,n);
	return 0;
    }
}

extern inline int psm_copy_to_user_or_kernel(int kern,void*to, const void *from,
			       unsigned long n )
{
    if (!kern)
	return copy_to_user(to,from,n);
    else{
	memcpy(to,from,n);
	return 0;
    }
}
       



#endif /* _PSM_MODULE_H_ */













