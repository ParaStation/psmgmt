#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pmi.h"

#define NUM_PUTS 4

#define VAL_LEN 256

#define NUM_ROUNDS 4

int name_len_max, key_len_max, value_len_max;

bool doKVSTest(char *KVSname, int rank, int size, char *valStr)
{
    char key[key_len_max];
    for (int i = 0; i < NUM_PUTS; i++) {
	snprintf(key, sizeof(key), "string-%d-%d", rank, i);
	int rc = PMI_KVS_Put(KVSname, key, valStr);
	printf("[%d] i/rc are %d/%d\n", rank, i, rc);
    }

    PMI_KVS_Commit(KVSname);

    PMI_Barrier();

    for (int i = 0; i < size; i++) {
	for (int j = 0; j < NUM_PUTS; j++) {
	    char kvsVal[value_len_max];
	    snprintf(key, sizeof(key), "string-%d-%d", i, j);

	    int rc = PMI_KVS_Get(KVSname, key, kvsVal, value_len_max);

	    if (rc != PMI_SUCCESS || strncmp(kvsVal, valStr, value_len_max)) {
		printf("[%d] Failed (rank = %d put = %d value = %s rc = %d)\n",
		       rank, i, j, kvsVal, rc);
		fflush(stdout);

		return false;
	    }
	}
    }

    return true;
}

int main(void)
{
    int has_parent;
    int rc = PMI_Init(&has_parent);
    if (rc != PMI_SUCCESS) return -1;

    int rank;
    PMI_Get_rank(&rank);
    int size;
    PMI_Get_size(&size);

    printf("[%d] size %d %sparent\n", rank, size, has_parent ? "" : "no ");

    PMI_KVS_Get_name_length_max(&name_len_max);
    PMI_KVS_Get_key_length_max(&key_len_max);
    PMI_KVS_Get_value_length_max(&value_len_max);

    //value_len_max = 1000; // @todo
    //if (!rank) printf("name/key/value max are %d/%d/%d\n", name_len_max,
    //key_len_max, value_len_max); // @todo
    printf("[%d] name/key/value max are %d/%d/%d\n", rank, name_len_max,
	   key_len_max, value_len_max);

    char kvs_name[name_len_max];
    PMI_KVS_Get_my_name(kvs_name, name_len_max);

    char value[value_len_max];
    int len = (VAL_LEN) ? VAL_LEN : value_len_max;
    for (int i = 0; i < len - 1; i++) value[i] = 33 + i %(127-33);
    value[len - 1] = '\0';

    // printf("[%d] val is '%s'\n", rank, value);

    bool result = true;
    for (int i = 0; i < NUM_ROUNDS && result; i++) {
	result = doKVSTest(kvs_name, rank, size, value);

	if (result) printf("[%d] Round %d: Pass\n", rank, i);
    }

    if (result) {
	printf("[%d] Pass\n", rank);
	fflush(stdout);
    }

    PMI_Finalize();

    return 0;
}
