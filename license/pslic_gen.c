#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <sys/types.h>
#include <unistd.h>

#include <popt.h>

/* MCP Key: */
#include <netinet/in.h>
#include "ps_types.h"
#include "license_priv.h"

/* Daemon Hash */
#include "pslic_hidden.h"

/* ps tools */
#include "psstrings.h"

/* Dont compile this file inside the build folder ->
   We dont have the library libputil here. We include
   all needed functions directly */

#include "pslic.c"
#include "psstrings.c"

int arg_verbose = 0;
int arg_createhash = 0;
int arg_createmcpkey = 0;
int arg_createlicense = 0;
int arg_check = 0;
int arg_shortoutput = 0;

void usage(poptContext optCon, int exitcode, char *error, char *addl)
{
    poptPrintUsage(optCon, stderr, 0);
    if (error) fprintf(stderr, "%s: %s\n", error, addl);
    exit(exitcode);
}

void parse_opt(int argc, char **argv)
{
    char    c;            /* used for argument parsing */
    poptContext optCon;   /* context for parsing command-line options */
    
    struct poptOption optionsTable[] = {
	{ "hash" , 'a', POPT_ARGFLAG_OR, &arg_createhash, 0,
	  "Create the license hash", "" },
	{ "mcp" , 'm', POPT_ARGFLAG_OR, &arg_createmcpkey, 0,
	  "Create the mcp licensekey", "" },
	{ "lic" , 'l', POPT_ARGFLAG_OR, &arg_createlicense, 0,
	  "Create License", "" },
	{ "short" , 's', POPT_ARGFLAG_OR, &arg_shortoutput, 0,
	  "Omit the fieldnames in output", "" },
	{ "check" , 'c', POPT_ARGFLAG_OR, &arg_check, 0,
	  "Check a license key", "" },
	{ "verbose"  , 'v', POPT_ARG_INT, &arg_verbose , 0,
	  "be more verbose", "level" },
/*	{ "flag" , 'f', POPT_ARGFLAG_OR, &arg_flag, 0,
	  "flag description", "" },*/
	POPT_AUTOHELP
	{ NULL, 0, 0, NULL, 0, NULL, NULL }
    };
    
    optCon = poptGetContext(NULL, argc,(const char **) argv, optionsTable, 0);

    if (argc < 2) {
	poptPrintUsage(optCon, stderr, 0);
	exit(1);
    }
    
    /* Now do options processing, get portname */
    while ((c = poptGetNextOpt(optCon)) >= 0) {
    }
    if (c < -1) {
	/* an error occurred during option processing */
	fprintf(stderr, "%s: %s\n",
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		poptStrerror(c));
	poptPrintHelp(optCon, stderr, 0);
	exit(1);
    }
    
    poptFreeContext(optCon);
}


void check_mcpkey(char *Key)
{
    static pslic_bin_t LicKey;
    pslic_binpub_t LicPub;

    char *from;
    char *to;
    unsigned int i;
    
    if (!Key) return;
    
    pslic_ascii2bin((unsigned char *) Key,
		    pslic_blen2alen(sizeof(LicPub))
		    ,(unsigned char *)&LicPub);

    from=(char*)&(LicPub);
    to=(char*)(&LicKey);

    for (i=0; i < sizeof(pslic_bin_t); i++) *to++=*from++;
    
    pslic_decode(&LicKey,sizeof(LicKey),LIC_KEYMAGIC);

    LicKey.Nodes     = htonl(LicKey.Nodes);
    LicKey.ValidFrom = htonl(LicKey.ValidFrom);
    LicKey.ValidTo   = htonl(LicKey.ValidTo);
    LicKey.Magic     = htonl(LicKey.Magic);

    fprintf(stderr, "MCPKey: LicMagic : %s\n",
	    LicKey.Magic == LIC_KEYMAGIC ? "OK" : "FALSE");
    fprintf(stderr, "MCPKey: ValidFrom: %d\n", LicKey.ValidFrom);
    fprintf(stderr, "MCPKey: ValidTo  : %d\n", LicKey.ValidTo);
    fprintf(stderr, "MCPKey: Nodes    : %d\n", LicKey.Nodes);
}


void do_createmcpkey(env_fields_t *env)
{
#define BLEN (pslic_blen2alen(sizeof(pslic_bin_t)))
    struct timeval tv;
    pslic_bin_t lic;
    char licstr[ BLEN + 1];
    
    lic.Nodes	= lic_numval(env, LIC_NODES, 0);
    lic.ValidFrom = str_datetotime_d(env_get(env, LIC_DATE), 0);
    /* Unlimited is 40 years :-) . Changed to maxint*/
    lic.ValidTo = str_datetotime_d(env_get(env,LIC_EXPIRE),
				   INT32_MAX
				   /*lic.ValidFrom + 3600 * 24 * 365 * 40*/);

    if (arg_verbose) {
	fprintf(stderr, "MCPKey for %d nodes valid from %d to %d\n",
		lic.Nodes, lic.ValidFrom, lic.ValidTo);
    }

    lic.Magic	  = LIC_KEYMAGIC;

    lic.Nodes     = htonl(lic.Nodes);
    lic.ValidFrom = htonl(lic.ValidFrom);
    lic.ValidTo   = htonl(lic.ValidTo);
    lic.Magic     = htonl(lic.Magic);

    pslic_encode(&lic,sizeof(lic),LIC_KEYMAGIC);
    licstr[BLEN]=0;
    pslic_bin2ascii((char*)&lic,sizeof(lic),licstr);

    env_set(env, LIC_MCPKEY, licstr);
}

void do_createhash(env_fields_t *env)
{
    char *fl = env_get(env, LIC_FIELDLIST);
    char *hs;

    if (!fl) return; /* error */

    hs = lic_calchash(env, fl);
    if (!hs) return; /* error */

    env_set(env, LIC_HASH, hs);
}

int main(int argc, char **argv)
{
    env_fields_t env;
    int ret;
    char **t;

    parse_opt(argc, argv);

    env_init(&env);
    ret = lic_fromfile(&env, "-"); /* Read from stdin */

    if (ret < 0){
	fprintf(stderr, "lic_fromfile: %s\n", lic_errstr ? lic_errstr : "unknown");
	exit(1);
    }

    if (arg_verbose) {
	fprintf(stderr, "Read vars:\n");
	for (t = env.vars; t && *t; t++){
	    fprintf(stderr, "%s\n", *t);
	}
	fprintf(stderr, "\n");
    }

    if (arg_createlicense) {
	char lic[30];
	if (env_get(&env, LIC_LICENSE)) {
	    printf("ERROR: "LIC_LICENSE" already included\n");
	    exit(1);
	}

	snprintf(lic, sizeof(lic), "%d-%d-%d", getpid(), getuid(),
		 (int)(((time(0) / 3600)+2) % 24)); /* :-) */
	env_set(&env, LIC_LICENSE, lic);

	printf("%s\"%s\"\n",
	       arg_shortoutput ? "" : LIC_LICENSE " = ",
	       env_get(&env, LIC_LICENSE));
	if (arg_verbose) {
	    fprintf(stderr, "Created "LIC_LICENSE":<%s>\n", env_get(&env, LIC_LICENSE));
	}
    }

    if (arg_createmcpkey) {
	if (env_get(&env, LIC_MCPKEY)) {
	    printf("ERROR: "LIC_MCPKEY" already included\n");
	    exit(1);
	}
	do_createmcpkey(&env);

	if (!env_get(&env, LIC_MCPKEY)) {
	    printf("ERROR: Create "LIC_MCPKEY" failed\n");
	    exit(1);
	}

	printf("%s\"%s\"\n",
	       arg_shortoutput ? "" : LIC_MCPKEY " = ",
	       env_get(&env, LIC_MCPKEY));
	if (arg_verbose) {
	    fprintf(stderr, "Created "LIC_MCPKEY":<%s>\n", env_get(&env, LIC_MCPKEY));
	}
    }

    if (arg_createhash) {
	if (env_get(&env, LIC_HASH)) {
	    printf("ERROR: "LIC_HASH" already included\n");
	    exit(1);
	}
	do_createhash(&env);

	if (!env_get(&env, LIC_HASH)) {
	    printf("ERROR: Create "LIC_HASH" failed\n");
	    exit(1);
	}

	printf("%s%s\n",
	       arg_shortoutput ? "" : LIC_HASH " = ",
	       env_get(&env, LIC_HASH));
	if (arg_verbose) {
	    fprintf(stderr, "Created "LIC_HASH":<%s>\n", env_get(&env, LIC_HASH));
	}
    }

    if (arg_verbose || arg_check) {
	/* Check for the presents of some Fields */
	fprintf(stderr, "%10s available : %s\n",
		LIC_DATE, env_get(&env, LIC_DATE) ? "YES" : "NO" );
	fprintf(stderr, "%10s available : %s\n",
		LIC_EXPIRE, env_get(&env, LIC_EXPIRE) ? "YES" : "NO" );
	fprintf(stderr, "%10s available : %s\n",
		LIC_NODES, env_get(&env, LIC_NODES) ? "YES" : "NO" );
	fprintf(stderr, "%10s available : %s\n",
		LIC_CPUs, env_get(&env, LIC_CPUs) ? "YES" : "NO" );

	/* Check hash */
	fprintf(stderr, "%10s correct : %s\n",
		LIC_HASH, lic_isvalid(&env) ? "YES" : "NO" );
	/* check dates */
	fprintf(stderr, "    Key up to date : %s\n",
		!lic_isexpired(&env) ? "YES" : "NO" );

	check_mcpkey(env_get(&env, LIC_MCPKEY));
    }
    
    return 0;
}

/*
 * Local Variables:
 *  compile-command: "gcc pslic_gen.c -Wall -W -Wno-unused -g -O2 -I../include -I../lib/putil -lpopt -o pslic_gen"
 * End:
 *
 */
