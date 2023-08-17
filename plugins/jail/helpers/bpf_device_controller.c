#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

#define MAX_MAP_ENTRIES 4096

/* used as hash to store BPF access */
typedef struct {
  int major;	    /** device major */
  int minor;	    /** device minor */
} BPF_key_t;

struct bpf_map_def SEC("maps") device_map = {
    .type = BPF_MAP_TYPE_HASH,
    .key_size = sizeof(BPF_key_t),
    .value_size = sizeof(int),
    .max_entries = MAX_MAP_ENTRIES,
};

SEC("cgroup/dev")
int bpf_prog(struct bpf_cgroup_dev_ctx *ctx) {

    BPF_key_t key = { ctx->major, ctx->minor };
    int *value = bpf_map_lookup_elem(&device_map, &key);
    if (value && !*value) {
	/* deny access */
	return 0;
    }

    /* allow access */
    return 1;
}
