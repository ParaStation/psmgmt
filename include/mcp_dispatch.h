
#ifndef _MCP_DISPATCH_H_
#define _MCP_DISPATCH_H_



struct dispatch_table {
    UINT8   _space[ 2 << LAST_STATE_BIT  ];
    UINT8   event[1];  // event table !! use negative(or 0) index (mapped in _space)!! 
    MCP_POINTER(void)  event_func[ NR_EVENT_FUNC ];
#ifdef DISPATCHTIMER
    UINT32  lastrtc;
//    UINT32  lastoff;
    UINT32  count[ NR_EVENT_FUNC ];
    UINT32  time[ NR_EVENT_FUNC ];
#endif
};

#ifdef __lanai__

extern struct dispatch_table dt;

//  //#define ASMDISPATCH

//  #ifdef ASMDISPATCH
//  /* By hand coding this, we save 1 insn.  The L7 compiler does an
//     optimal job on the rest of the dispatch. */


//  #define DISPATCH do {
//    UINT32 offset;
//    offset = RTC /*-12 DISPATCH_OFFSET(get_ISR())*/;
//    asm volatile ("ld [%0 add %1],%?pc ! begin hand coded DISPATCH\n\t"
//                  "nop\n\t"
//                  "nop\n\t"
//                  "nop\n\t"
//                  "nop\n\t !end handcoded DISPATCH"
//  		/* */ :
//                  /* */ : "r" (gmptr), "r" (offset)
//                  /* */ : "memory");
//    goto L_never_get_here;
//  } while (0)
//  #endif


#ifdef DISPATCHTIMER
#  define DISPTIMER(fromev)						\
{									\
    ASMC("EVENT " SUB_N_STRINGIFY((fromev)) " Disptimer begin");	\
    dt.time[(fromev)]+= RTC - dt.lastrtc;				\
    dt.count[(fromev)]++;						\
    dt.lastrtc = RTC;							\
    ASMC("EVENT " SUB_N_STRINGIFY((fromev)) " Disptimer end");		\
}
    

#define REG_EVENT_DT(_eventnr)			\
      dt.count[(_eventnr)]=0;			\
      dt.time[(_eventnr)]=0;
      
#else /*DISPATCHTIMER*/
#  define DISPTIMER(fromev)
#  define REG_EVENT_DT(_eventnr)
#endif

/* Default C dispatch */
#ifndef DISPATCH
#define DISPATCH_(fromev,state) do {					\
  UINT32 _offset;							\
  void *_handler;							\
  _offset = dt.event[-(state)];						\
  DISPTIMER((fromev));							\
  _handler = *(void **)((char *) dt.event_func + _offset);		\
/*  TRACEONE("DISPATCH",(_handler));*/					\
  goto *_handler;							\
} while (0)
#endif

#define DISPATCH(fromev) DISPATCH_((fromev), (ISR & IMR)| mcp_mem.State  )


/* Register Event (bind eventnr to eventlabel) */
#define REG_EVENT( _eventnr , _eventlabel ) do{				\
  dt.event_func[(_eventnr)] = &&_eventlabel;				\
  REG_EVENT_DT((_eventnr))						\
  /*for debug */							\
  /*event_names[ _eventnr ] = #_eventlabel;*/                           \
  }while(0)

/* Loop to bind state to event */
#define EVENT_LOOP_BEGIN do {						\
  int state;								\
  for (state = 0 ; state < (2 << LAST_STATE_BIT);state++){		

/* End of loop. Set default eventnr */ 
#define EVENT_LOOP_END(default_eventnr)				\
  EVENT_SET( state , (default_eventnr) );  }} while (0);

/* for use in EVENT_LOOP. If cond then bind state to eventnr */
#define EVENT_LOOP_IF( cond , _eventnr )	\
      if ( cond ) {				\
  EVENT_SET( state , (_eventnr) );		\
  continue;					\
    }
/* Bind eventnr to state */
#define EVENT_SET( state , _eventnr ) \
      dt.event[-(state)] =  (_eventnr) * sizeof(void *);

#endif /* __lanai__ */


#endif /* _MCP_DISPATCH_H_ */





