
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int n;
    int i;
    char *x;
    struct timespec ts;

    n = strtol(argv[1], NULL, 10);
    x = malloc(n);

    read(STDIN_FILENO, x, n);
    close(STDIN_FILENO);

    memset(&ts, 0, sizeof(ts));
    ts.tv_sec = 2;
    nanosleep(&ts, NULL);

    for (i = 0; i < n; ++i)
        putchar(x[i]);
    putchar('\n');

    return 0;
}

