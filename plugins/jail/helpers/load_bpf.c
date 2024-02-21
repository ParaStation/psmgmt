#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <popt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/bpf.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#define BPF_PSID_PATH "/sys/fs/bpf/psid"

/* used as hash to store BPF access */
typedef struct {
  int major;	    /** device major */
  int minor;	    /** device minor */
} BPF_key_t;

/* BPF key to save access type in BPF map */
static BPF_key_t bpfKey = { -1, -1 };

/* devices access type saved in BPF map */
static int bpfAccess = -1;

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

/* only report errors */
static int quiet = 0;

/* remove allowed devices from BPF map */
static char *removeAllowed = NULL;

/**
 * @brief Ensure argument has the correct device format
 */
static void verifyDev(const char *arg)
{
    char *endptr;
    bpfKey.major = strtol(arg, &endptr, 10);
    if (*endptr != ':') {
	fprintf(stderr, "Invalid format for device specification. "
		"Use MAJOR:MINOR\n");
	exit(1);
    }

    bpfKey.minor = strtol(endptr + 1, &endptr, 10);
    if (*endptr != '\0') {
	fprintf(stderr, "Invalid format for device specification. "
		"Use MAJOR:MINOR\n");
	exit(1);
    }
}

/**
 * @brief Parse command line arguments
 */
static void parseArgs(int argc, const char *argv[])
{
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
	{ "quiet", 'q', POPT_ARG_NONE, &quiet, 0,
	  "Be quiet", NULL },
	{ "remove_allowed", '\0', POPT_ARG_STRING, &removeAllowed, 0,
	  "Remove allowed devices from given BPF map", "ID" },
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
	fprintf(stderr, "Use --load_prog in combination with --attach_path\n");
	exit(1);
    }

    if (!progID) {
	fprintf(stderr, "--prog_id is mandatory\n");
	exit(1);
    } else {
	/* remove additional map suffix */
	const char *suffix = "_map";
	size_t sufLen = strlen(suffix);
	size_t len = strlen(progID);
	if (len > sufLen) {
	    char *end = progID + (len - sufLen);
	    if (!strcmp(end, suffix)) end[0] = '\0';
	}
    }

    if (allow_dev && deny_dev) {
	fprintf(stderr, "--allow_dev and --deny_dev are mutual exclusive\n");
	exit(1);
    }

    if (allow_dev) {
	verifyDev(allow_dev);
	bpfAccess = true;
    } else if (deny_dev) {
	verifyDev(deny_dev);
	bpfAccess = false;
    }

    poptFreeContext(optCon);
}

/**
 * @brief Load a BPF program
 *
 * Load a BPF program and its corresponding map. The program is
 * attached to a cgroup and the map is pinned.
 */
static void loadBPF(void)
{
    if (!quiet) {
	printf("Attaching BPF program %s to %s\n", bpfProg, attachPath);
    }

    /* open bpf program (bytecode) and load it */
    struct bpf_object *obj = bpf_object__open_file(bpfProg, NULL);
    if (libbpf_get_error(obj)) {
	fprintf(stderr, "Failed to open BPF object %s\n", bpfProg);
	exit(1);
    }

    if (bpf_object__load(obj)) {
	fprintf(stderr, "Failed to load BPF object\n");
	exit(1);
    }

    /* retrive program file descriptor */
    struct bpf_program *prog;
    prog = bpf_object__find_program_by_name(obj, "bpf_prog");
    if (!prog) {
        fprintf(stderr, "Failed to find BPF program\n");
	exit(1);
    }

    int prog_fd = bpf_program__fd(prog);
    if (prog_fd < 0) {
        fprintf(stderr, "Failed to get BPF program FD\n");
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

    /* attach BPF program to cgroup */
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

    if (!quiet) {
	printf("BPF program %s successful attached to %s\n", bpfProg,
	       attachPath);
    }
}

/**
 * @brief Add or update an element in the BPF map
 */
static void updateMap()
{
    if (bpf_map_update_elem(mapFD, &bpfKey, &bpfAccess, BPF_ANY) != 0) {
	fprintf(stderr, "Failed to update BPF map with key %i:%i fd %i: %s\n",
		bpfKey.major, bpfKey.minor, mapFD, strerror(errno));
	exit(1);
    }

    if (!quiet) {
	printf("Saved access to device %i:%i in map\n", bpfKey.major,
	       bpfKey.minor);
    }
}

/**
 * @brief Open a BPF map from its pinned location
 */
static int openMap(const char *id)
{
    char pinPath[PATH_MAX];
    snprintf(pinPath, sizeof(pinPath), "%s/%s_map", BPF_PSID_PATH, id);
    int fd = bpf_obj_get(pinPath);
    if (fd < 0) {
	fprintf(stderr, "Failed to open map %s: %s\n", pinPath,
		strerror(errno));
	exit(1);
    }

    return fd;
}

/**
 * @brief Reopen the BPF map from its pinned location
 */
static void reopenMap(void)
{
    if (mapFD != -1) return;
    mapFD = openMap(progID);
}

int main(int argc, const char *argv[])
{
    parseArgs(argc, argv);

    if (clearMap) {
	char pinPath[PATH_MAX];
	snprintf(pinPath, sizeof(pinPath), "%s/%s_map", BPF_PSID_PATH, progID);
	if (unlink(pinPath) == -1) {
	    if (!quiet) {
		fprintf(stderr, "Cleanup of map %s failed: %s\n", pinPath,
			strerror(errno));
	    }

	    return 1;
	}
	return 0;
    }

    if (showDev || removeAllowed) {
	/* open existing BPF map */
	reopenMap();

	int destMap = -1;
	if (removeAllowed) destMap = openMap(removeAllowed);

	/* loop over all configured devices */
	BPF_key_t key = { -1, -1}, nextKey;
	while (!bpf_map_get_next_key(mapFD, &key, &nextKey)) {
	    key.major = nextKey.major;
	    key.minor = nextKey.minor;
	    int value;
	    if (bpf_map_lookup_elem(mapFD, &key, &value) < 0) continue;

	    if (showDev) {
		/* print output only */
		fprintf(stdout, "Access to device %i:%i is %s\n", key.major,
			key.minor, (value ? "allowed" : "denied"));
	    } else {
		/* remove access */
		if (removeAllowed && !value) continue;

		/* deny access */
		value = 0;
		if (bpf_map_update_elem(destMap, &key, &value, BPF_ANY) != 0) {
		    fprintf(stderr, "Failed to update BPF map with key %i:%i"
			    " fd %i: %s\n", key.major, key.minor,
			    destMap, strerror(errno));
		    exit(1);
		}
	    }
	}
	return 0;
    }

    /* allow to load and modify the map simultaneously */
    if (bpfProg) loadBPF();

    /* if access was specified save it to BPF map */
    if (bpfAccess != -1) {
	if (mapFD < 0) reopenMap();

	/* update access rights in map */
	updateMap();
    }

    return 0;
}
