#include <stdlib.h>

#include <slurm/spank.h>

SPANK_PLUGIN(nopshcspank, 1)

static int _enable_nopshc = 0;

static int spank_option_cb(int val, const char *optarg, int remote)
{
    _enable_nopshc = 1;

    return 0;
}

static struct spank_option opts[] = {
    {"disable-pshealthcheck", NULL,
     "Disable prologue and epilogue pshealthcheck (experimental)",
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
    if (_enable_nopshc) {
	spank_job_control_setenv(sp, "SET_NO_PSHEALTHCHECK", "1", 1);
	spank_job_control_setenv(sp, "SLURM_SPANK_SET_NO_PSHEALTHCHECK", "1", 1);
    } else {
	spank_job_control_setenv(sp, "SET_NO_PSHEALTHCHECK", "0", 1);
	spank_job_control_setenv(sp, "SLURM_SPANK_SET_NO_PSHEALTHCHECK", "0", 1);
    }

    return 0;
}
