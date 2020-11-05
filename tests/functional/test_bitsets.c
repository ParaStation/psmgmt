/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>

#include "pscpu.h"

int main(void)
{
    PSCPU_set_t set1, set2;

    PSCPU_clrAll(set1);
    PSCPU_setCPU(set1, 3);
    PSCPU_setCPU(set1, 5);
    PSCPU_setCPU(set1, 11);
    PSCPU_setCPU(set1, 17);
    PSCPU_setCPU(set1, 23);

    PSCPU_clrAll(set2);
    PSCPU_setCPU(set2, 17);

    printf("set2 is %s\n", PSCPU_print_part(set2, 4));
    printf("%s bits set in set2\n", PSCPU_any(set2, PSCPU_MAX) ? "some" : "no");
    printf("%s bits set amongst first %d in set2\n",
	   PSCPU_any(set2, 16) ? "some" : "no", 16);
    printf("%s bits set amongst first %d in set2\n",
	   PSCPU_any(set2, 17) ? "some" : "no", 17);

    printf("\n\n");
    PSCPU_setCPU(set2, 0);
    printf("set1 is %s\n", PSCPU_print_part(set1, 4));
    printf("set2 is %s\n", PSCPU_print_part(set2, 4));

    printf("set1 and set2 are %sdisjoint\n",
	   PSCPU_disjoint(set1, set2, PSCPU_MAX) ? "" : "not ");

    printf("first 11 bits of set1 and set2 are %sdisjoint\n",
	   PSCPU_disjoint(set1, set2, 11) ? "" : "not ");
    printf("first 17 bits of set1 and set2 are %sdisjoint\n",
	   PSCPU_disjoint(set1, set2, 17) ? "" : "not ");
    printf("first 18 bits of set1 and set2 are %sdisjoint\n",
	   PSCPU_disjoint(set1, set2, 18) ? "" : "not ");

    printf("\n\n");
    PSCPU_clrAll(set1);
    PSCPU_setCPU(set1, 3);
    PSCPU_setCPU(set1, 5);

    PSCPU_clrAll(set2);
    PSCPU_setCPU(set2, 4);
    PSCPU_setCPU(set2, 5);

    printf("set1 is %s\n", PSCPU_print_part(set1, 2));
    printf("set2 is %s\n", PSCPU_print_part(set2, 2));

    printf("set1 and set2 are %sdisjoint\n",
	   PSCPU_disjoint(set1, set2, PSCPU_MAX) ? "" : "not ");

    printf("first 5 bits of set1 and set2 are %sdisjoint\n",
	   PSCPU_disjoint(set1, set2, 5) ? "" : "not ");
    printf("first 6 bits of set1 and set2 are %sdisjoint\n",
	   PSCPU_disjoint(set1, set2, 6) ? "" : "not ");

    printf("\n\n");
    PSCPU_clrAll(set1);
    PSCPU_setCPU(set1, 3);
    PSCPU_setCPU(set1, 5);

    PSCPU_clrAll(set2);
    PSCPU_setCPU(set2, 2);
    PSCPU_setCPU(set2, 4);
    PSCPU_setCPU(set2, 127);

    printf("set1 is %s\n", PSCPU_print_part(set1, 2));
    printf("set2 is %s\n", PSCPU_print_part(set2, 2));
    printf("set2 is %s\n", PSCPU_print_part(set2, 16));

    printf("set1 and set2 are %sdisjoint\n",
	   PSCPU_disjoint(set1, set2, PSCPU_MAX) ? "" : "not ");

    return 0;
}
