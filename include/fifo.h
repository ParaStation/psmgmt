

#ifndef PS_FIFO_H
#define PS_FIFO_H

#define PS_FIFO_STRUCT( entryt , size )		\
struct{						\
    unsigned Count;				\
    unsigned Head;				\
    entryt   Entrys[size];			\
}

#define PS_FIFO_INIT(fifo)				\
    (fifo).Head = (fifo).Count=0


#define PS_FIFO_SIZE( fifo )				\
    (sizeof((fifo).Entrys)/sizeof((fifo).Entrys[0]))

/* Put Entry 0:on success, 1: on failure */
#define PS_FIFO_PUT(fifo,entry)					\
    ({								\
	int res;						\
	if ((fifo).Count < PS_FIFO_SIZE(fifo)){			\
	    (fifo).Entrys[((fifo).Head +			\
		(fifo).Count)%PS_FIFO_SIZE(fifo)]= entry;	\
	    (fifo).Count++;					\
	    res=0;						\
	}else{							\
	    res=1;						\
	}							\
	res;							\
    })

/* Get Entry &Entry[]: on success, 0:on failure */
#define PS_FIFO_GET(fifo)					\
    ({								\
	typeof((fifo).Entrys[0]) *res;				\
	if ((fifo).Count ){					\
	    res=&(fifo).Entrys[(fifo).Head];			\
	    (fifo).Head = ((fifo).Head+1)%PS_FIFO_SIZE(fifo);	\
	    (fifo).Count--;					\
	}else{							\
	    res = 0;						\
	}							\
	res;							\
    })

/* Get Entry Entry[]: on success, default:on failure */
#define PS_FIFO_GETD(fifo,default)				\
    ({								\
	typeof((fifo).Entrys[0]) res;				\
	if ( (fifo).Count ){					\
	    res=(fifo).Entrys[(fifo).Head];			\
	    (fifo).Head = ((fifo).Head+1)%PS_FIFO_SIZE(fifo);	\
	    (fifo).Count--;					\
	}else{							\
	    res=default;					\
	}							\
	res;							\
    })



#endif
