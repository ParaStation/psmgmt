
void pty_setowner(uid_t uid, gid_t gid, const char *tty);
void pty_make_controlling_tty(int *ttyfd, const char *tty);
