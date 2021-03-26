#include <stdbool.h>
#include <stdlib.h>

#include <slurm/spank.h>

SPANK_PLUGIN(perfparanoidspank, 1)
const char psid_plugin[] = "yes";

static bool _enable_nonparanoid = false;

static int spank_option_cb(int val, const char *optarg, int remote)
{
    _enable_nonparanoid = true;

    return 0;
}

static struct spank_option opts[] = {
    {"disable-perfparanoid", NULL,
     "Setting Kernel not to be paranoid with respect to perf ",
     0, 0, spank_option_cb},
    SPANK_OPTIONS_TABLE_END
};

int slurm_spank_init(spank_t sp, int ac, char **av)
{
    spank_option_register(sp, opts);

    return 0;
}

int slurm_spank_init_post_opt(spank_t sp, int ac, char **av)
{
    if (_enable_nonparanoid) {
	spank_job_control_setenv(sp, "SET_NON_PERF_PARANOID", "1", 1);
	spank_job_control_setenv(sp, "SLURM_SPANK_SET_NON_PERF_PARANOID", "1",
				 1);
    } else{
	spank_job_control_setenv(sp, "SET_NON_PERF_PARANOID", "0", 1);
	spank_job_control_setenv(sp, "SLURM_SPANK_SET_NON_PERF_PARANOID", "0",
				 1);
    }
    return 0;
}
