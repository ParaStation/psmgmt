/* arg.h: definitions for argument parsing package */

/* Modified Version */

#ifndef ARG_HDR
#define ARG_HDR

/* form type values */
#define ARG_REGULAR	1	/* a regular argument */
#define ARG_SIMPFLAG	2	/* a simple flag (no parameters) */
#define ARG_PARAMFLAG	3	/* a flag with parameters */
#define ARG_SUBRFLAG	4	/* a flag with subroutine action */
#define ARG_SUBLISTFLAG	5	/* a sub-formlist */
#define ARG_NOP		6	/* no arg or flag, just a doc string */

/* the following must be impossible pointer values (note: machine-dependent) */
#define ARG_MASKNEXT	0x80000000	/* mask for these NEXT flags */
#define ARG_FLAGNEXT	0x80000001
#define ARG_SUBRNEXT	0x80000002
#define ARG_LISTNEXT	0x80000003

/* varargs tricks */
#define ARG_FLAG(ptr)		ARG_FLAGNEXT, (ptr)	/* for SIMPFLAG */
#define ARG_SUBR(ptr)		ARG_SUBRNEXT, (ptr)	/* for SUBRFLAG */
#define ARG_SUBLIST(ptr)	ARG_LISTNEXT, (ptr)	/* for SUBLISTFLAG */

/* error codes: BADCALL is a programmer error, the others are user errors */
#define ARG_BADCALL	-1	/* arg_parse call itself is bad */
#define ARG_BADARG	-2	/* bad argument given */
#define ARG_MISSING	-3	/* argument or parameter missing */
#define ARG_EXTRA	-4	/* extra argument given */

#define ARG_NARGMAX 10000	/* max number of allowed args */

extern int arg_debug, arg_doccol;
extern int arg_warning;		/* print warnings about repeated flags? */
//Arg_form *arg_to_form1(), *arg_find_flag(), *arg_find_reg();

typedef struct Arg_form Arg_form;

#ifdef __cplusplus
    extern "C" {
#endif	
	int arg_parse(int ac, char **av, ...);
	int arg_parse_argv(int ac, char **av, Arg_form *form);
	int arg_parse_stream(FILE *fp, Arg_form *form);
	Arg_form *arg_to_form(char *arg,...);
	int arg_form_print(Arg_form *form);
	int expr_eval_int(char *str);
	long expr_eval_long(char *str);
	double expr_eval(char *str);
#ifdef __cplusplus
    }
#endif

#endif



