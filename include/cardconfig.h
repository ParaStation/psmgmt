
/* 2001-08-31 (c) ParTec AG ,Jens Hauke */

#ifndef _CARDCONFIG_H_
#define _CARDCONFIG_H_


typedef struct card_init_T{
    int		node_id;
    char	*licensekey; /* NULL for evaluation */
    char	*module;     /* full path to psm.o */
    char	*options;    /* extra module options or NULL */
    char	*routing_file; /* full path to routing file */
}card_init_t;

int card_init(card_init_t *ic);
int card_cleanup(card_init_t *ic);

/* return str of last error */
char *card_errstr(void);




#endif
