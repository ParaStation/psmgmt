#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <linux/bpf.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

typedef struct {
  bool allow;	    /** flag to grant or deny access */
  int major;	    /** device major */
  int minor;	    /** device minor */
} BPF_data_t;

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <BPF-program> <attach-path> <allow> "
		"<major> <minor>\n",
		argv[0]);
        return 1;
    }

    int prog_fd;
    struct bpf_object *obj;
    if (bpf_prog_load(argv[1], BPF_PROG_TYPE_CGROUP_DEVICE, &obj,
	&prog_fd) != 0) {
        fprintf(stderr, "Failed to load BPF program %s\n", argv[1]);
        return 1;
    }

    struct bpf_map *map = bpf_object__find_map_by_name(obj, "device_map");
    if (!map) {
	fprintf(stderr, "BPF device_map not found\n");
	return 1;
    }

    int map_fd = bpf_map__fd(map);
    if (map_fd < 0) {
        fprintf(stderr, "Failed to get file descriptor for BPF map\n");
        return 1;
    }

    bool allow = atoi(argv[3]);
    int major = atoi(argv[4]);
    int minor = atoi(argv[5]);

    BPF_data_t value = { allow, major, minor };
    int key = 0;

    if (bpf_map_update_elem(map_fd, &key, &value, BPF_ANY) != 0) {
        fprintf(stderr, "Failed to update BPF map\n");
        return 1;
    }

    char *attachPath = argv[2];
    int cgroup_fd = open(attachPath, O_DIRECTORY);
    if (cgroup_fd < 0) {
        fprintf(stderr, "Failed to open cgroup %s\n", attachPath);
        return 1;
    }

    int ret = bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_DEVICE, 0);
    if (ret < 0) {
        fprintf(stderr, "Failed to attach BPF program to cgroup %s\n",
		attachPath);
        return 1;
    }

    printf("loaded BPF device controller\n");
    return 0;
}
