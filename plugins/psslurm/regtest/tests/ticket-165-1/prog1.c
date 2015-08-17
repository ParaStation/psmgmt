
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int n, i;
    char *x;
    struct timespec ts;

    n = strtol(argv[1], NULL, 10);
    x = malloc(n);
    memset(x, 'a', n);

    write(STDOUT_FILENO, x, n);

    return 0;
}

