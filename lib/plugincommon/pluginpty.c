/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <grp.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>

#include "pscommon.h"
#include "pluginlog.h"
#include "pluginpty.h"

#define _PATH_TTY "/dev/tty"

void pty_setowner(uid_t uid, gid_t gid, const char *tty)
{
    struct group *grp;
    mode_t mode;
    struct stat st;

    /* Determine the group to make the owner of the tty. */
    grp = getgrnam("tty");
    if (grp) {
	gid = grp->gr_gid;
	mode = S_IRUSR | S_IWUSR | S_IWGRP;
    } else {
	mode = S_IRUSR | S_IWUSR | S_IWGRP | S_IWOTH;
    }

    if (stat(tty, &st)) {
	pluginlog("%s: stat(%s) : %s", __func__, tty, strerror(errno));
	exit(1);
    }

    if (st.st_uid != uid || st.st_gid != gid) {
	if (chown(tty, uid, gid) < 0) {
	    pluginlog("%s: chown(%s) : %s", __func__, tty, strerror(errno));
	    exit(1);
	}
    }

    if ((st.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO)) != mode) {
	if (chmod(tty, mode) < 0) {
	    pluginlog("%s: chmod(%s) : %s", __func__, tty, strerror(errno));
	    exit(1);
	}
    }
}

void pty_make_controlling_tty(int *ttyfd, const char *tty)
{
    int fd;
    void *oldCONT, *oldHUP;

    /* first disconnect from the old controlling tty */
    fd = open(_PATH_TTY, O_RDWR | O_NOCTTY);
    if (fd >= 0) {
	if (ioctl(fd, TIOCNOTTY, NULL)<0) {
	    pluginlog("%s: ioctl(TIOCNOTTY) on %s failed: %s\n",
		__func__, tty, strerror(errno));
	}
	close(fd);
    }

    fd = open(_PATH_TTY, O_RDWR | O_NOCTTY);
    if (fd >= 0) {
	pluginlog("%s: still connected to controlling tty\n", __func__);
	close(fd);
    }

    /* make it the controlling tty */
#ifdef TIOCSCTTY
    if (ioctl(*ttyfd, TIOCSCTTY, 1) < 0)
	pluginlog("%s: ioctl(TIOCSCTTY) on %s failed : %s\n",
	__func__, tty, strerror(errno));
#else
	pluginlog("%s: no TIOCSCTTY available\n", __func__);
#error No TIOCSCTTY
#endif /* TIOCSCTTY */

    oldCONT = PSC_setSigHandler(SIGCONT, SIG_IGN);
    oldHUP = PSC_setSigHandler(SIGHUP, SIG_IGN);
    if (vhangup() < 0) {
	pluginlog("vhangup()\n");
    }
    PSC_setSigHandler(SIGCONT, oldCONT);
    PSC_setSigHandler(SIGHUP, oldHUP);

    if ((fd = open(tty, O_RDWR)) < 0) {
	pluginlog("%s: open(%s) : %s\n", __func__, tty, strerror(errno));
    } else {
	close(*ttyfd);
	*ttyfd = fd;
    }
    /* verify that we now have a controlling tty */
    if ((fd = open(_PATH_TTY, O_WRONLY)) < 0) {
	pluginlog("%s: unable set controlling tty: open(%s) : %s\n",
	    __func__, _PATH_TTY, strerror(errno));
    } else {
	close(fd);
    }
}
