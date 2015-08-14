
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

static void Read(int fd, void *buf, int size)
{
	int x;

	while (size > 0) {
		x = read(fd, buf, size);
		if (-1 == x) {
			if (EINTR == errno)
				continue;

			abort();
		}

		buf  += x;
		size -= x;
	}
}

int main(int argc, char **argv)
{
	int n;
	int i, z;
	char *x;
	struct timespec ts;

	n = strtol(argv[1], NULL, 10);
	x = malloc(n);

	Read(STDIN_FILENO, x, n);
	close(STDIN_FILENO);

	memset(&ts, 0, sizeof(ts));
	ts.tv_sec = 2;
	nanosleep(&ts, NULL);

	for (i = 0; i < n; ++i) {
		z = putchar(x[i]);
		if (x[i] != z)
			abort();
	}
	z = putchar('\n');
	if ('\n' != z)
		abort();

	return 0;
}

