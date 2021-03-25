#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <slurm/spank.h>

SPANK_PLUGIN(visspank, 1)

static char *_start_xserver = NULL;

static char *_supported[] = {
    "0",
    "1",
    "0,1",
    NULL
};

static int spank_option_cb(int val, const char *optarg, int remote)
{
    if (NULL == optarg) {
	_start_xserver = strdup("0,1");
    } else {
	_start_xserver = strdup(optarg);
    }

    bool ok = false;
    for (int i = 0; _supported[i]; ++i) {
	if (!strcmp(_supported[i], _start_xserver)) ok = true;
    }

    if (!ok) {
	slurm_error("Invalid argument %s to option --start-xserver",
		    _start_xserver);
	return -1;
    }

    return 0;
}

static struct spank_option opts[] = {
    {"start-xserver", NULL,
     "Start an Xserver for usage with VirtualGL", 2, 0, spank_option_cb},
    SPANK_OPTIONS_TABLE_END
};

int slurm_spank_init(spank_t sp, int ac, char **av)
{
    spank_option_register(sp, opts);

    return 0;
}

int slurm_spank_init_post_opt(spank_t sp, int ac, char **av)
{
    if (_start_xserver) {
	spank_job_control_setenv(sp, "START_XSERVER", _start_xserver, 1);
	spank_job_control_setenv(sp, "SLURM_SPANK_START_XSERVER",
				 _start_xserver, 1);
    }

    return 0;
}
