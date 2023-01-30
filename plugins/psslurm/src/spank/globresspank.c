#include <stddef.h>  // IWYU pragma: keep

#include <slurm/spank.h>

/*
 * All spank plugins must define this macro for the
 * Slurm plugin loader.
 */
SPANK_PLUGIN(globresspank, 1)
const char psid_plugin[] = "yes";

/*
 * Callbacks for SPANK option
 * Currently empty, but we can add functionality later.
 */
static int _globres_option_cb(int val, const char *optarg, int remote)
{
    return 0;
}

/*
 *  Provide submission option:
 *    --globres=res_list
 */
static struct spank_option opts[] = {
    { "globres", "<type>:<resource>[:<count>],..",
      "Comma-separated list of required global resources",
      1, 0, (spank_opt_cb_f) _globres_option_cb },
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
