#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pmi.h"

#define VAL_LEN 256

#define NUM_ROUNDS 4


void checkPMI(int round)
{
    int has_parent;
    int rc = PMI_Init(&has_parent);
    if (rc != PMI_SUCCESS) {
	printf("[?] PMI_Init() failed in round %d\n", round);
	return;
    }

    int rank;
    PMI_Get_rank(&rank);
    int size;
    PMI_Get_size(&size);

    printf("[%d] size %d %sparent in round %d\n", rank, size,
	   has_parent ? "" : "no ", round);

    int name_len_max = 0, key_len_max = 0, value_len_max = 0;
    PMI_KVS_Get_name_length_max(&name_len_max);
    PMI_KVS_Get_key_length_max(&key_len_max);
    PMI_KVS_Get_value_length_max(&value_len_max);

    printf("[%d] name/key/value max are %d/%d/%d\n", rank, name_len_max,
	   key_len_max, value_len_max);

    char kvs_name[name_len_max];
    PMI_KVS_Get_my_name(kvs_name, name_len_max);
    printf("[%d] my name is '%s'\n", rank, kvs_name);

    printf("[%d] Round %d: %spassed\n", rank, round,
	   PMI_Finalize() ? "not ": "");
}

int main(void)
{
    char *rankStr = getenv("PMI_RANK");
    int rank = rankStr ? atoi(rankStr) : 0;

    for (int i = 0; i < NUM_ROUNDS; i++) {
	checkPMI(i);
	usleep(200000);
    }

    if (rank) {
	usleep(5000000);
	printf("(%d) done\n", rank);
	return 0;
    } else {
	printf("(%d) done\n", rank);
	return -1;
    }
}
