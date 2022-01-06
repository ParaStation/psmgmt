#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <slurm/spank.h>

SPANK_PLUGIN(x11spank, 1)
const char psid_plugin[] = "yes";

#define XAUTH		"/usr/bin/xauth"
#define X11_FIRST_PORT	6010
#define X11_LAST_PORT	6100

/* In order to avoid that slurm_spank_exit is called twice we
 * store the pid of the main srun process.
 */
static pid_t srun_pid = -1;
static pid_t forwarder_pid = -1;
static int enable_forwarding = 0;

#define MAX(X, Y)	(((X) >= (Y)) ? (X) : (Y))
#define MIN(X, Y)	(((X) >= (Y)) ? (Y) : (X))

/*
 * An X11 display consisting of the host (may be NULL) without
 * trailing zero, the length of the host string (again without trailing
 * zero), the display and the screen.
 */
struct x11display {
    const char *host;
    size_t hostlen;
    int	display;
    int	screen;
};

/*
 * Parse the DISPLAY variable and return the result in the argument.
 * We follow the Slurm/SPANK conventions in that the function returns
 * a negative value in the case of an error.
 *
 * d->host points to the user environment and thus should not be freed.
 *
 * We use strtol to convert the strings to integers. Unfortunately
 * not all implementations set errno in case of a conversion error.
 * Therefore we have no reliable way to check if the conversion succeeded.
 * Unless the user fiddles with the environment this should not be
 * too much of a problem. In the worst case, if $DISPLAY is trash we
 * will not be able to connect to the X server.
 */
static int parse_display_env(struct x11display *d)
{
    const char *display = getenv("DISPLAY");
    if (!display) {
	slurm_error("%s: DISPLAY variable not set", __func__);
	return -1;
    }

    const char *p = strchr(display, ':');
    if (!p) {
	slurm_error("%s: DISPLAY variable is malformed", __func__);
	return -2;
    }

    if (display == p) {
	d->host    = NULL;
	d->hostlen = 0;
    } else {
	d->host	   = display;
	d->hostlen = p - display;
    }

    char buf[256];
    size_t n = strlen(p);
    if (n > sizeof(buf)) {
	slurm_error("%s: DISPLAY variable is too long", __func__);
	return -3;
    }

    memcpy(buf, p + 1, n);

    char *q = strchr(buf, '.');
    if (q) {
	*(q++) = 0;

	errno = 0;
	d->screen = strtol(q, NULL, 10);
	if (errno) {
	    slurm_error("%s: invalid screen value in DISPLAY", __func__);
	    return -4;
	}
    } else {
	d->screen = 0;
    }

    errno = 0;
    d->display = strtol(buf, NULL, 10);
    if (errno) {
	slurm_error("%s: invalid display value in DISPLAY", __func__);
	return -5;
    }

#ifdef DEBUG
    slurm_info("DISPLAY:");

    if (d->host) {
	*(char *)mempcpy(buf, d->host, MIN(sizeof(buf)-1, d->hostlen)) = 0;
    } else {
	*(char *)mempcpy(buf, "(null)", MIN(sizeof(buf)-1, 6)) = 0;
    }

    slurm_info("\thost    = %s", buf);
    slurm_info("\tdisplay = %d", d->display);
    slurm_info("\tscreen  = %d", d->screen);
#endif

    return 0;
}

/*
 * Retrieve the protocol and cookie using the xauth program.
 */
int get_xauth_proto_and_cookie(struct x11display *d, char *proto, int protolen,
			       char *cookie, int cookielen)
{
    char str[2048];

    if (d->hostlen >= sizeof(str)) {
	slurm_error("%s: hostname is too long", __func__);
	return -1;
    }

    const char *display = getenv("DISPLAY");
    if (!display) {
	slurm_error("%s: DISPLAY variable not set", __func__);
	return -1;
    }

    snprintf(str, sizeof(str), "%s list %s", XAUTH, display);

#ifdef DEBUG
    slurm_info("Executing '%s'", str);
#endif

    FILE *f = popen(str, "r");
    if (!f) {
	slurm_error("%s: executing '%s' failed", __func__, str);
	return -4;
    }

    /* It is safe to reuse str at this point due to COW */
    snprintf(str, sizeof(str), "%%*s %%%ds %%%ds", protolen, cookielen);

    int err = fscanf(f, str, proto, cookie);
    if (err != 2) {
	slurm_error("%s: reading xauth output failed", __func__);
	return -5;
    }

    err = pclose(f);
    if (err) {
	slurm_error("%s: pclose returned %d", __func__, err);
	return -6;
    }

#ifdef DEBUG
    slurm_info("proto  = %s", proto);
    slurm_info("cookie = %s", cookie);
#endif

    return 0;
}

/*
 * Linked list of socket pairs.
 */
struct sockpair {
    int left;
    int right;
    struct sockpair	*next;
};

/*
 * Connect to the X server using an internet domain socket.
 */
int connect_x_inet(struct x11display *d)
{
    char hostname[256];

    if (d->hostlen > sizeof(hostname) - 1) {
	slurm_error("%s: hostname is too long", __func__);
	return -1;
    }

    *(char *)mempcpy(hostname, d->host, d->hostlen) = 0;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags    = AI_CANONNAME;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *ailist;
    if (getaddrinfo(hostname, NULL, &hints, &ailist)) {
	slurm_error("%s: getaddrinfo() failed", __func__);
	return -3;
    }

    struct sockaddr_in sa;
    struct sockaddr_in *sai;
    struct addrinfo *aip;

    aip = ailist;
    sai = (struct sockaddr_in *)aip->ai_addr;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr = sai->sin_addr;
    sa.sin_port = htons(6000 + d->display);

    freeaddrinfo(aip);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
	slurm_error("%s: socket creation failed", __func__);
	return -4;
    }

    if (connect(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	slurm_error("%s: connect to %s:%d failed: %s", __func__,
		    inet_ntoa(sa.sin_addr), ntohs(sa.sin_port),
		    strerror(errno));
	close(sock);
	return -5;
    }

    int one = 1;
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one))) {
	slurm_error("%s: setsockopt(TCP_NODELAY) failed", __func__);
	/* continue anyway */
    }

    return sock;
}

/*
 * Connect to the X server using an unix domain socket.
 */
int connect_x_unix(struct x11display *d)
{
    struct sockaddr_un sa;

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "/tmp/.X11-unix/X%d",
	     d->display);

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
	slurm_error("%s: socket creation failed", __func__);
	return -4;
    }

    int err = connect(sock, (struct sockaddr *)&sa, sizeof(sa));
    if (err < 0) {
	slurm_error("%s: connect failed: '%s'", __func__, strerror(errno));
	close(sock);
	return -5;
    }

    return sock;
}

/*
 * Connect to the X server.
 */
int connect_x(struct x11display *d)
{
    return (d->host) ? connect_x_inet(d) : connect_x_unix(d);
}

/*
 * Copy data from one socket to another.
 */
int copy(int from, int to, void *buf, int bufsize)
{
    errno = 0;
    int n = read(from, buf, bufsize);
    if (n < 1) {
#ifdef DEBUG
	slurm_error("%s: read(%i) failed: %s\n", __func__, from,
		    strerror(errno));
#endif
	return -1;
    }

    errno = 0;
    int m = write(to, buf, n);
    if (m < 1) {
#ifdef DEBUG
	slurm_error("%s: write(%i) failed: %s\n", __func__, to,
		    strerror(errno));
#endif
	return -2;
    }

    if (n != m) {
	slurm_error("%s: write size mismatch", __func__);
	return -3;
    }

    return 0;
}

/*
 * Remove an entry from a linked list.
 */
void remove_list_entry(struct sockpair *head, struct sockpair *sp)
{
    struct sockpair *sq;
    struct sockpair **pp;

    pp = &head->next;
    for (sq = head->next; (sq != sp) && sq; sq = sq->next) {
	pp = &(*pp)->next;
    }

    *pp = sp->next;
}

/*
 * Infinite forwarder loop. Accept connections from the mother superior
 * and forward traffic.
 */
__attribute__((noreturn))
void forwarder_loop(struct x11display *d, int sock)
{
    char buf[512];

    struct sockpair head;
    memset(&head, 0, sizeof(head));

    while (1) {
	fd_set readset;
	FD_ZERO(&readset);

	FD_SET(sock, &readset);
	int n = sock;

	struct sockpair *sp;
	for (sp = head.next; sp; sp = sp->next) {
	    FD_SET(sp->left , &readset);
	    n = MAX(n, sp->left);

	    FD_SET(sp->right, &readset);
	    n = MAX(n, sp->right);
	}

	int err = select(n + 1, &readset, NULL, NULL, NULL);
	if ((err < 0) && (EINTR == errno))
	    continue;

	if (err < 0) {
	    slurm_error("select failed");
	}

	if (FD_ISSET(sock, &readset)) {
	    sp = &head;
	    while (sp->next) {
		sp = sp->next;
	    }

	    sp->next = malloc(sizeof(struct sockpair));
	    if (!sp->next) {
		slurm_error("malloc failed.");
		goto fail;
	    }
	    sp = sp->next;

	    memset(sp, 0, sizeof(*sp));

	    sp->left = accept(sock, NULL, NULL);
	    if (sp->left < 0) {
		slurm_error("accept failed");
		goto fail;
	    }

	    sp->right = connect_x(d);
	    if (sp->right < 0) {
		slurm_error("Failed to connect to X server");
		goto fail;
	    }
	}

	/* interate safely so we can remove list entries on the fly */
	struct sockpair *nxt;
	for (sp = head.next, nxt = sp ? sp->next : sp; sp;
	     sp = nxt, nxt = sp ? sp->next : sp) {
	    if (FD_ISSET(sp->left , &readset)) {
		err = copy(sp->left, sp->right, buf, sizeof(buf));
		if (err < 0) {
		    close(sp->left);
		    close(sp->right);

		    remove_list_entry(&head, sp);
		    free(sp);

		    continue;
		}
	    }
	    if (FD_ISSET(sp->right, &readset)) {
		err = copy(sp->right, sp->left, buf, sizeof(buf));
		if (err < 0) {
		    close(sp->left);
		    close(sp->right);

		    remove_list_entry(&head, sp);
		    free(sp);

		    continue;
		}
	    }
	}
    }

fail:
    slurm_error("%s: forwarder failed. Going into infinite loop.", __func__);
    while (1);
}

/*
 * Open a socket for the forwarding of the X11 traffic. We open a port
 * in a range that allows the remote side to attach the X application
 * directly to the forwarder process.
 */
int open_forwarder_socket()
{
    int sock = -1;

    for (short p = X11_FIRST_PORT; p <= X11_LAST_PORT; ++p) {
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
	    continue;
	}

	struct sockaddr_in sai;
	memset(&sai, 0, sizeof(sai));
	sai.sin_family = AF_INET;
	sai.sin_addr.s_addr = INADDR_ANY;
	sai.sin_port = htons(p);

	int err = bind(sock, (struct sockaddr *)&sai, sizeof(sai));
	if (err < 0) {
	    close(sock);
	    sock = -1;
	    continue;
	}

	break;
    }

    if (sock < 0) {
	slurm_error("Failed to open socket for forwarding.");
	return -1;
    }

    int err = listen(sock, 6);
    if (err < 0) {
	slurm_error("listen failed");
	return -2;
    }

    return sock;
}

/*
 * Fork a forwarder process. Return the pid and the socket address.
 */
int fork_forwarder(struct x11display *d, pid_t *pid, struct sockaddr_in *sai,
                   socklen_t alen)
{
    *pid = -1;

    int sock = open_forwarder_socket();
    if (sock < 0) return -1;

    memset(sai, 0, alen);
    socklen_t blen = alen;

    int err = getsockname(sock, sai, &blen);
    if (err < 0) {
	slurm_error("%s: getsockname() failed", __func__);
	return -2;
    }

    if (alen != blen) {
	slurm_error("%s: getsockname() address size mismatch", __func__);
	return -3;
    }

    pid_t p = fork();
    if (p == 0) {
	forwarder_loop(d, sock);
    } else {
	if (p == -1) {
	    slurm_error("%s: fork() failed", __func__);
	    return -4;
	}

	close(sock);

	*pid = p;
#ifdef DEBUG
	slurm_info("%s: forwarder pid = %d", __func__, *pid);
#endif
    }

    return 0;
}

static int spank_option_cb(int val, const char *optarg, int remote)
{
    enable_forwarding = 1;
    return 0;
}

static struct spank_option opts[] = {
    {"forward-x", NULL, "Enable X forwarding.", 0, 0, spank_option_cb},
    SPANK_OPTIONS_TABLE_END
};

int slurm_spank_init(spank_t sp, int ac, char **av)
{
    if (S_CTX_LOCAL == spank_context()) spank_option_register(sp, opts);

    return 0;
}

int slurm_spank_job_prolog(spank_t sp, int ac, char **av)
{
#ifdef DEBUG
    FILE *f = fopen("/tmp/x11spank", "w");
    if (!f) {
	slurm_error("Could not open file '/tmp/x11spank' for writing");
	return -1;
    }

    fprintf(f, "%s\n", getenv("SPANK_X11_HOST"));
    fprintf(f, "%s\n", getenv("SPANK_X11_PORT"));
    fprintf(f, "%s\n", getenv("SPANK_X11_PROTO"));
    fprintf(f, "%s\n", getenv("SPANK_X11_COOKIE"));
    fprintf(f, "%s\n", getenv("SPANK_X11_SCREEN"));

    fclose(f);
#endif

    return 0;
}

int slurm_spank_local_user_init(spank_t sp, int ac, char **av)
{
    if (!enable_forwarding) return 0;

    srun_pid = getpid();

#ifdef DEBUG
    slurm_info("%s: X11 forwarding enabled", __func__);
#endif

    struct x11display d;
    int ret = parse_display_env(&d);
    if (ret < 0) {
	slurm_error("%s: parsing display env failed\n", __func__);
	return ret;
    }

    char proto[64], cookie[64];
    ret = get_xauth_proto_and_cookie(&d, proto, sizeof(proto),
	    cookie, sizeof(cookie));
    if (ret < 0) {
	slurm_error("%s: calling xauth failed\n", __func__);
	return ret;
    }

    struct sockaddr_in sai;
    ret = fork_forwarder(&d, &forwarder_pid, &sai, sizeof(sai));
    if (ret <0) {
	slurm_error("%s: forking forwarder failed\n", __func__);
	return ret;
    }

    char addr[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &sai.sin_addr, addr, sizeof(addr))) {
	slurm_error("%s: inet_ntop() failed", __func__);
	return -1;
    }
#ifdef DEBUG
    slurm_info("%s: SPANK_X11_HOST = %s", __func__, addr);
#endif
    spank_job_control_setenv(sp, "X11_HOST", addr, 1);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int )ntohs(sai.sin_port));
#ifdef DEBUG
    slurm_info("%s: SPANK_X11_PORT = %s", __func__, buf);
#endif
    spank_job_control_setenv(sp, "X11_PORT", buf, 1);
    spank_job_control_setenv(sp, "X11_PROTO" , proto, 1);
    spank_job_control_setenv(sp, "X11_COOKIE", cookie, 1);

    snprintf(buf, sizeof(buf), "%d", (int )d.screen);
    spank_job_control_setenv(sp, "X11_SCREEN", buf, 1);

    return 0;
}

int slurm_spank_exit(spank_t sp, int ac, char **av)
{
    if (S_CTX_LOCAL != spank_context()) return 0;
    if (getpid() != srun_pid) return 0;

    if (forwarder_pid != -1) {
#ifdef DEBUG
	slurm_info("%s: killing forwarder pid %d", __func__, forwarder_pid);
#endif
	kill(forwarder_pid, SIGTERM);

	struct timespec ts;

	ts.tv_sec  = 1;
	ts.tv_nsec = 0;
	nanosleep(&ts, NULL);

	kill(forwarder_pid, SIGKILL);

	int status;
	waitpid(forwarder_pid, &status, 0);
    }

    return 0;
}
