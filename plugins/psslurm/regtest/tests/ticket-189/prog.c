
#include <stdio.h>

int main(int argc, char **argv)
{
	char line[4096];

	while (1) {
		if (NULL == fgets(line, 4096, stdin))
			break;

		fprintf(stdout, line);
		fflush (stdout);
	}

	return 0;
}

