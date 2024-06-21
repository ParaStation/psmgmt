#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <slurm/slurm_errno.h>
#include <slurm/spank.h>

SPANK_PLUGIN (psetspank, 1);
const char psid_plugin[] = "yes";

static char *lastPSet = NULL;

static int spank_option_cb(int val, const char *optarg, int remote)
{
    if (optarg) lastPSet = strdup(optarg);

    return 0;
}

struct spank_option spank_options[] = {
    {"pset", "name",
     "User-specified name assigned to processes in the current group",
     1, 0, spank_option_cb, },
    SPANK_OPTIONS_TABLE_END,
};

int slurm_spank_init_post_opt(spank_t sp, int ac, char **av)
{
    static int component = 0;
    if (lastPSet) {
	char key[32];
	sprintf(key, "SLURM_SPANK_PSET_%i", component);
	setenv(key, lastPSet, 1);
	free(lastPSet);
	lastPSet = NULL;
    }
    component++;

    return ESPANK_SUCCESS;
}
