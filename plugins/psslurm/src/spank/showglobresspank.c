#include <stdlib.h>

#include <slurm/spank.h>

/*
 * All spank plugins must define this macro for the
 * Slurm plugin loader.
 */
SPANK_PLUGIN(showglobresspank, 1)
const char psid_plugin[] = "yes";

/*
 * Callbacks for SPANK option
 * Currently empty, but we can add functionality later.
 */
static int _show_globres_option_cb(int val, const char *optarg, int remote)
{
    return 0;
}

/*
 *  Provide submission option:
 *    --show-globres
 */
static struct spank_option opts[] = {
    { "show-globres", NULL,
      "Show configured and available global resources",
	0, 0, (spank_opt_cb_f) _show_globres_option_cb },
    SPANK_OPTIONS_TABLE_END
};

/*
 * We only want to register the SPANK options for the local and the
 * allocator context. No extra functionality.
 */
int slurm_spank_init(spank_t sp, int ac, char **av)
{
    if (spank_context() == S_CTX_ALLOCATOR || spank_context() == S_CTX_LOCAL) {
	spank_option_register(sp, opts);
    }

    return 0;
}
