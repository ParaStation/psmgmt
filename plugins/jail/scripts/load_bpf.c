#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <popt.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#include <linux/bpf.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#define BPF_PSID_PATH "/sys/fs/bpf/psid"

#define MAX_MAP_ENTRIES 1024

typedef struct {
  int allow;	    /** flag to grant or deny access */
  int major;	    /** device major */
  int minor;	    /** device minor */
} BPF_access_t;

/* path to BPF program to load */
static char *bpfProg = NULL;

/* path to attach the BPF program to */
static char *attachPath = NULL;

/* unique identifier used to identify a BPF map */
static char *progID = NULL;

/* list alll configured devices */
static int showDev = 0;

/* cleanup BPF map */
static int clearMap = 0;

/* file descriptor of the BPF map */
static int mapFD = -1;

/* devices access type saved into BPF map */
static BPF_access_t bpfAccess = { -1, -1, -1 };


/**
 * @brief Ensure argument has the correct device format
 */
void verifyDev(const char *arg) {
    char *endptr;

    bpfAccess.major = strtol(arg, &endptr, 10);
    if (*endptr != ':') {
        fprintf(stderr, "Invalid format for device specification. "
		"Use MAJOR:MINOR\n");
        exit(1);
    }

    bpfAccess.minor = strtol(endptr + 1, &endptr, 10);
    if (*endptr != '\0') {
        fprintf(stderr, "Invalid format for device specification. "
		"Use MAJOR:MINOR\n");
        exit(1);
    }
}

/**
 * @brief Parse command line arguments
 */
void parseArgs(int argc, const char *argv[]) {
    char *allow_dev = NULL;
    char *deny_dev = NULL;

    struct poptOption options[] = {
        { "load_prog", 'l', POPT_ARG_STRING, &bpfProg, 0,
	  "Load a BPF program", "OBJ_PATH" },
        { "attach_path", 'p', POPT_ARG_STRING, &attachPath, 0,
	  "The path to attach the BPF program", "ATTACH_PATH" },
        { "prog_id", 'i', POPT_ARG_STRING, &progID, 0,
	  "A unique identifier for the program", "UNIQUE_ID" },
        { "allow_dev", 'a', POPT_ARG_STRING, &allow_dev, 0,
	  "Allow access to devices", "MAJOR:MINOR" },
        { "deny_dev", 'd', POPT_ARG_STRING, &deny_dev, 0,
	  "Deny access to devices", "MAJOR:MINOR" },
        { "show_dev", 's', POPT_ARG_NONE, &showDev, 0,
	  "Show access to devices", NULL },
        { "clear", 'c', POPT_ARG_NONE, &clearMap, 0,
	  "Cleanup BPF map after use", NULL },
	POPT_AUTOHELP {NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    poptContext optCon = poptGetContext(NULL, argc, argv, options, 0);
    int rc;

    while ((rc = poptGetNextOpt(optCon)) >= 0) {}

    if (rc < -1) {
        fprintf(stderr, "%s: %s\n",
                poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
                poptStrerror(rc));
        exit(1);
    }

    if (argc < 2) {
	poptPrintUsage(optCon, stderr, 0);
	exit(0);
    }

    if ((bpfProg || attachPath) && (!bpfProg || !attachPath)) {
	fprintf(stderr, "Use --load_prog in combination with --attachPath\n");
	exit(1);
    }

    if (!progID) {
	fprintf(stderr, "--progID is manditory\n");
	exit(1);
    }

    if (allow_dev && deny_dev) {
	fprintf(stderr, "--allow_dev and --deny_dev are mutual exclusive\n");
	exit(1);
    }

    if (allow_dev) {
        verifyDev(allow_dev);
	bpfAccess.allow = true;
    } else if (deny_dev) {
        verifyDev(deny_dev);
	bpfAccess.allow = false;
    }

    poptFreeContext(optCon);
}

/**
 * @brief Load a BPF program
 *
 * Load a BPF program and its corresponding map. The program is
 * attached to a cgroup and the map is pinned.
 */
void loadBPF(void)
{
    printf("Attaching BPF program %s to %s\n", bpfProg, attachPath);

    int prog_fd;
    struct bpf_object *obj;
    if (bpf_prog_load(bpfProg, BPF_PROG_TYPE_CGROUP_DEVICE, &obj,
	&prog_fd) != 0) {
        fprintf(stderr, "Failed to load BPF program %s: %s\n", bpfProg,
		strerror(errno));
	exit(1);
    }

    /* open BPF map */
    struct bpf_map *map = bpf_object__find_map_by_name(obj, "device_map");
    if (!map) {
	fprintf(stderr, "open BPF device_map failed: %s\n", strerror(errno));
	exit(1);
    }

    mapFD = bpf_map__fd(map);
    if (mapFD < 0) {
        fprintf(stderr, "Failed to get file descriptor for BPF map: %s\n",
		strerror(errno));
	exit(1);
    }

    /* attach BPF prog to cgroup */
    int cgroup_fd = open(attachPath, O_DIRECTORY);
    if (cgroup_fd < 0) {
        fprintf(stderr, "Failed to open cgroup %s: %s\n", attachPath,
		strerror(errno));
	exit(1);
    }

    int ret = bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_DEVICE, 0);
    if (ret < 0) {
        fprintf(stderr, "Failed to attach BPF program to cgroup %s: %s\n",
		attachPath, strerror(errno));
	exit(1);
    }

    /* pin the BPF map to enable later changes (e.g. add/remove devices) */
    if (mkdir(BPF_PSID_PATH, 0755) < 0) {
	if (errno != EEXIST) {
	    fprintf(stderr, "mkdir(%s) failed: %s\n", BPF_PSID_PATH,
		    strerror(errno));
	    exit(1);
	}
    }

    char pinPath[PATH_MAX];
    snprintf(pinPath, sizeof(pinPath), "%s/%s_map", BPF_PSID_PATH, progID);

    if (bpf_obj_pin(mapFD, pinPath) < 0) {
        fprintf(stderr, "Failed to pin BPF map to %s: %s\n", pinPath,
		strerror(errno));
	exit(1);
    }
}

/**
 * @brief Add or update an element in the BPF map
 */
void updateMap(int *key, BPF_access_t *value)
{
    if (bpf_map_update_elem(mapFD, key, value, BPF_ANY) != 0) {
	fprintf(stderr, "Failed to update BPF map %i fd %i: %s\n", *key,
		mapFD, strerror(errno));
	exit(1);
    }

    printf("Saved access to device %i:%i in map element %i\n", value->major,
	   value->minor, *key);
}

/**
 * @brief Reopen the BPF map from its pinned location
 */
static void reopenMap()
{
    if (mapFD != -1) return;

    char pinPath[PATH_MAX];
    snprintf(pinPath, sizeof(pinPath), "%s/%s_map", BPF_PSID_PATH, progID);
    mapFD = bpf_obj_get(pinPath);
    if (mapFD == -1) {
	fprintf(stderr, "Failed to open map %s: %s\n", pinPath,
		strerror(errno));
	exit(1);
    }
}

/**
 * @brief Find a specified devices in the BPF map
 */
static int findDev()
{
    /* try to find device in BPF map to update */
    int key = -1, nextKey;
    while (!bpf_map_get_next_key(mapFD, &key, &nextKey)) {
	key = nextKey;
	BPF_access_t value;
	if (!bpf_map_lookup_elem(mapFD, &key, &value)) {
	    if (value.major == bpfAccess.major &&
		value.minor == bpfAccess.minor) {
		return key;
	    }
	}
    }

    return -1;
}

/**
 * @brief Save access to a devices in the BPF map
 * */
void setBPFAccess(void)
{
    if (mapFD != -1) {
	/* BPF program was loaded therefore the mapFD is already
	 * attached. Save device access in empty BPF map */
	int key = 0;
	updateMap(&key, &bpfAccess);
	return;
    }

    /* open existing BPF map */
    reopenMap();

    int key = findDev();
    if (key != -1) {
	/* update access rights in map */
	updateMap(&key, &bpfAccess);
	return;
    }

    /* add new device to an empty slot in BPF map */
    for (int i=0; i<MAX_MAP_ENTRIES; i++) {
	BPF_access_t value;
	if (bpf_map_lookup_elem(mapFD, &i, &value) == -1) {
	    if (errno == ENOENT) {
		updateMap(&i, &bpfAccess);
		break;
	    }
	    fprintf(stderr, "Failed to lookup element %i in map: %s\n",
		    i, strerror(errno));
	}
    }
}

int main(int argc, const char *argv[]) {

    parseArgs(argc, argv);

    if (clearMap) {
	char pinPath[PATH_MAX];
	snprintf(pinPath, sizeof(pinPath), "%s/%s_map", BPF_PSID_PATH, progID);
	if (unlink(pinPath) == -1) {
	    fprintf(stderr, "Cleanup of map %s failed: %s\n", pinPath,
		    strerror(errno));

	    return 1;
	}
	return 0;
    }

    if (showDev) {
	/* open existing BPF map */
	reopenMap();

	/* show all configured devices */
	int key = -1, nextKey;
	while (!bpf_map_get_next_key(mapFD, &key, &nextKey)) {
	    key = nextKey;
	    BPF_access_t value;
	    if (!bpf_map_lookup_elem(mapFD, &key, &value)) {
		fprintf(stdout, "Access to device %i:%i is %s\n", value.major,
			value.minor, (value.allow ? "allowed" : "denied"));
	    }
	}
	return 0;
    }

    /* allow to load and modify the map simultaneously */
    if (bpfProg) {
	/* load a BPF program */
	loadBPF();
    }

    if (bpfAccess.allow != -1) {
	/* change access to a devices */
	setBPFAccess();
    }

    return 0;
}
