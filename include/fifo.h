

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
    {								\
	if ((fifo).Count < PS_FIFO_SIZE(fifo)){			\
	    (fifo).Entrys[((fifo).Head +			\
		(fifo).Count)%PS_FIFO_SIZE(fifo)]= entry;	\
	    (fifo).Count++;					\
	}/*else error!*/					\
    }

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

#define PS_FIFO_GET_RES(fifo, res)				\
    {								\
	if ((fifo).Count ){					\
	    res=&(fifo).Entrys[(fifo).Head];			\
	    (fifo).Head = ((fifo).Head+1)%PS_FIFO_SIZE(fifo);	\
	    (fifo).Count--;					\
	}else{							\
	    res = 0;						\
	}							\
    }

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

/* Get Entry Entry[]: on success, default:on failure */
#define PS_FIFO_GETD_RES(fifo,default,res)			\
    {								\
	if ( (fifo).Count ){					\
	    res=(fifo).Entrys[(fifo).Head];			\
	    (fifo).Head = ((fifo).Head+1)%PS_FIFO_SIZE(fifo);	\
	    (fifo).Count--;					\
	}else{							\
	    res=default;					\
	}							\
    }


#endif
