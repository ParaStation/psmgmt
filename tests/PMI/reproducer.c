#include <stdio.h>
#include <stdlib.h>

#include "pmi.h"

#define NUM_PUTS 2

int main(void)
{
    int i, j;
    int rank, size, has_parent;
    int name_len_max, key_len_max, value_len_max;
    char *kvs_name = NULL, *key = NULL, *value = NULL;
    char *kvs_val = NULL;

    PMI_Init(&has_parent);
    PMI_Get_rank(&rank);
    PMI_Get_size(&size);

    PMI_KVS_Get_name_length_max(&name_len_max);
    PMI_KVS_Get_key_length_max(&key_len_max);
    PMI_KVS_Get_value_length_max(&value_len_max);

    //value_len_max = 1000; // @todo
    if (!rank) printf("name/key/value max are %d/%d/%d\n", name_len_max,
		      key_len_max, value_len_max);

    key = (char *) malloc(key_len_max * sizeof(char));
    value = (char *) malloc(value_len_max * sizeof(char));
    kvs_name = (char *) malloc(name_len_max * sizeof(char));
    kvs_val = (char *) malloc(value_len_max * sizeof(char));

    PMI_KVS_Get_my_name(kvs_name, name_len_max);

    for (i = 0; i < value_len_max - 1; i++) {
	value[i] = 'a';
    }
    value[value_len_max - 1] = '\0';

    for (i = 0; i < NUM_PUTS; i++) {
	snprintf(key, key_len_max, "string-%d-%d", rank, i);
	int rc = PMI_KVS_Put(kvs_name, key, value);
	printf("[%d] i/rc are %d/%d\n", rank, i, rc);
    }

    PMI_KVS_Commit(kvs_name);

    PMI_Barrier();

    for (i = 0; i < size; i++) {
	for (j = 0; j < NUM_PUTS; j++) {
	    snprintf(key, key_len_max, "string-%d-%d", i, j);

	    int rc = PMI_KVS_Get(kvs_name, key, kvs_val, value_len_max);

	    if (rc != PMI_SUCCESS || strncmp(kvs_val, value, value_len_max)) {
		printf("[%d] Failed (i = %d j = %d value = %s rc = %d)\n",
		       rank, i, j, kvs_val, rc);
		fflush(stdout);

		return -1;

	    }
	}
    }

    printf("[%d] Pass\n", rank);
    fflush(stdout);

    free(key);
    free(value);
    free(kvs_name);

    return 0;
}
