#include <stdlib.h>
#include <string.h>

#include <slurm/spank.h>

SPANK_PLUGIN(perftoolspank, 1)
const char psid_plugin[] = "yes";

static char *_perftool_list = NULL;

/* TODO Read list from spank arguments? */
static const char *_known_perftools[] = {"likwid", "vtune", NULL};

int validate_perftool_list()
{
    char *list = strdup(_perftool_list);
    int k = 0, n = 0;
    char *token = strtok(list, ",");

    while (token) {
	++n;
	for (int i = 0; _known_perftools[i]; ++i) {
	    if (!strcmp(_known_perftools[i], token)) ++k;
	}
	token = strtok(NULL, ",");
    }

    free(list);

    return (n > 0) && (n == k);
}

static int spank_option_cb(int val, const char *optarg, int remote)
{
    _perftool_list = strdup(optarg);

    return 0;
}

static struct spank_option opts[] = {
    {"perf-tool", "csl",
     "Enable performance analysis tools on nodes (experimental)",
     1, 0, spank_option_cb},
    SPANK_OPTIONS_TABLE_END
};

int slurm_spank_init(spank_t sp, int ac, char **av)
{
    spank_option_register(sp, opts);

    return 0;
}

int slurm_spank_init_post_opt(spank_t sp, int ac, char **av)
{
    if (_perftool_list) {
	if (!validate_perftool_list()) {
	    /* TODO Provide a --perf-tool help option to list the available
	     *      performance tools.
	     */
	    slurm_error("Invalid list of performance tools specified");
	    return -1;
	}
    }

    if (_perftool_list) {
	spank_job_control_setenv(sp, "PERF_TOOL_LIST", _perftool_list, 1);
	spank_job_control_setenv(sp, "SLURM_SPANK_PERF_TOOL_LIST",
				 _perftool_list, 1);
    } else {
	spank_job_control_setenv(sp, "PERF_TOOL_LIST", "", 1);
	spank_job_control_setenv(sp, "SLURM_SPANK_PERF_TOOL_LIST", "", 1);
    }

    return 0;
}
