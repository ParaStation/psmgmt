#ifndef __PSITASK_H__
#define __PSITASK_H__

#include <sys/types.h>

/*----------------------------------------------------------------------
 * Task Group constants
 */
#define TG_ANY        0x0001
#define TG_ADMIN      0x0002
#define TG_RESET      0x0003
#define TG_RESETABORT 0x0004

/*----------------------------------------------------------------------
 *   TaskOptions
 * Taskoptions are used to set the bits in the PSI_mychildoptions, which
 * influence the behaviour of a child being created.
 */
enum TaskOptions{
    TaskOption_ONESTDOUTERR   = 0x0001,
    /* use only one channel for stdout and stderr */
    TaskOption_SENDSTDHEADER  = 0x0002
    /* send a header (clientlogger_t) after connecting to stderr/stdout */
};

/*----------------------------------------------------------------------
 * Task types
 */
struct PSsignal_t{
    long tid;
    int signal;
    struct PSsignal_t* next;
};

#define PSTASKLIST_ENQUEUE(list,element) { element->link = list->link;\
					   element->rlink=list;\
					   list->link=element;\
					   element->link->rlink=element;}

typedef struct PStask_T{
    struct PStask_T* link;       /* link to the next task */
    struct PStask_T* rlink;      /* link to the previous task */
    long tid;                    /* task identifier */
    long ptid;                   /* parent tid */
    uid_t uid;                   /* user id */
    gid_t gid;                   /* group id */
    short nodeno;                /* node number of the executing node */
    long  group;                 /* task group number see TG_* constants */
    int masternode;              /* parent's node given for spwaned childs */
    int masterport;              /* parent's port given for spwaned childs */
    int rank;                    /* rank given for spwaned childs */
    unsigned int loggernode;     /* the logging peer for any output */
    int loggerport;              /* the logging peer for any output */
    short fd;                    /* connection fd of the psid, otherwise -1 */
    long error;                  /* error number */    
    long options;                /* options of this task */
    char* workingdir;            /* working directory */
    int argc;                    /* number of argv */
    char**argv;                  /* command line arguments */
    int environc;                /* number of environment variables */
    char**environ;               /* PULC Envvironment, used before spawning */
    int childsignal;             /* the signal which is sent when a child dies
				    previor to PULC_clientinit() */
    struct PSsignal_t* signalsender;   /* List of tasks which sent signals */
    struct PSsignal_t* signalreceiver; /* List of tasks which want to receive
					  a signals when this task dies */
}PStask_t;

/*----------------------------------------------------------------------
 * Task routines
 */
PStask_t *
PStask_new();                               /* returns a new task structure*/
int 
PStask_init(PStask_t *task);                /* initializes a task structure*/
int 
PStask_reinit(PStask_t *task);              /* reinitializes a task structure
					       that was previously used
					       remove allocated strings */
int 
PStask_delete(PStask_t * task);             /* deletes a task structure and all
					       strings associated with it */
void
PStask_sprintf(char*txt,PStask_t * task);   /* prints the task structure in a
					       string */
int
PStask_encode(char* buffer,PStask_t * task);/* encodes the task structure into
					       a string, so it can be sent */
int 
PStask_decode(char* buffer,PStask_t * task);/* decodes the task structure from
					       a string, maybe it was sent */

void PStask_setsignalreceiver(PStask_t* task, long sender, int signal);
long PStask_getsignalreceiver(PStask_t* task, int *signal);
void PStask_setsignalsender(PStask_t* task, long sender, int signal);
long PStask_getsignalsender(PStask_t* task, int *signal);


/*----------------------------------------------------------------------
 * Tasklist routines
 */
void
PStasklist_delete(PStask_t** list);

int
PStasklist_enqueue(PStask_t** list, PStask_t* newtask);

PStask_t*
PStasklist_dequeue(PStask_t** list, long tid);

PStask_t*
PStasklist_find(PStask_t* list, long tid);

#endif
