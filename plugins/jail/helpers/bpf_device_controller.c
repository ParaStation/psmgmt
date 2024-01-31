#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

#define MAX_MAP_ENTRIES 4096

/* used as hash to store BPF access */
typedef struct {
  int major;	    /** device major */
  int minor;	    /** device minor */
} BPF_key_t;

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_MAP_ENTRIES);
    __type(key, BPF_key_t);
    __type(value, int);
} device_map SEC(".maps");

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
