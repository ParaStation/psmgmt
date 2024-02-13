/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pluginpty.h"

#include <fcntl.h>
#include <grp.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pscommon.h"
#include "pluginlog.h"

#define _PATH_TTY "/dev/tty"

bool pty_setowner(uid_t uid, gid_t gid, const char *tty)
{
    /* Determine the group to make the owner of the tty. */
    mode_t mode;
    struct group *grp = getgrnam("tty");
    if (grp) {
	gid = grp->gr_gid;
	mode = S_IRUSR | S_IWUSR | S_IWGRP;
    } else {
	mode = S_IRUSR | S_IWUSR | S_IWGRP | S_IWOTH;
    }

    struct stat st;
    if (stat(tty, &st)) {
	pluginflog("stat(%s): %s", tty, strerror(errno));
	return false;
    }

    if (st.st_uid != uid || st.st_gid != gid) {
	if (chown(tty, uid, gid) < 0) {
	    pluginflog("chown(%s): %s", tty, strerror(errno));
	    return false;
	}
    }

    if ((st.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO)) != mode) {
	if (chmod(tty, mode) < 0) {
	    pluginflog("chmod(%s): %s", tty, strerror(errno));
	    return false;
	}
    }
    return true;
}

bool pty_make_controlling_tty(int *ttyfd, const char *tty)
{
    /* first disconnect from the old controlling tty */
    int fd = open(_PATH_TTY, O_RDWR | O_NOCTTY);
    if (fd >= 0) {
	if (ioctl(fd, TIOCNOTTY, NULL)<0) {
	    pluginflog("ioctl(TIOCNOTTY) on %s failed: %s\n",
		       tty, strerror(errno));
	}
	close(fd);
    }

    fd = open(_PATH_TTY, O_RDWR | O_NOCTTY);
    if (fd >= 0) {
	pluginflog("still connected to controlling tty\n");
	close(fd);
    }

    /* make it the controlling tty */
#ifdef TIOCSCTTY
    if (ioctl(*ttyfd, TIOCSCTTY, 1) < 0) {
	pluginflog("ioctl(TIOCSCTTY) on %s failed: %s\n", tty, strerror(errno));
	return false;
    }
#else
    pluginflog("no TIOCSCTTY available\n");
    return false;
#error No TIOCSCTTY
#endif /* TIOCSCTTY */

    void *oldCONT = PSC_setSigHandler(SIGCONT, SIG_IGN);
    void *oldHUP = PSC_setSigHandler(SIGHUP, SIG_IGN);
    if (vhangup() < 0) {
	pluginwarn(errno, "vhangup()");
	return false;
    }
    PSC_setSigHandler(SIGCONT, oldCONT);
    PSC_setSigHandler(SIGHUP, oldHUP);

    if ((fd = open(tty, O_RDWR)) < 0) {
	pluginflog("open(%s): %s\n", tty, strerror(errno));
	return false;
    } else {
	close(*ttyfd);
	*ttyfd = fd;
    }
    /* verify that we now have a controlling tty */
    if ((fd = open(_PATH_TTY, O_WRONLY)) < 0) {
	pluginflog("unable to set controlling tty: open(%s): %s\n",
		   _PATH_TTY, strerror(errno));
	return false;
    } else {
	close(fd);
    }
    return true;
}
