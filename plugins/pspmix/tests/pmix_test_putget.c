#include <stdio.h>

#include <pmix.h>


#define CHECK_ERROR(ret, func)                                                 \
do {                                                                           \
    printf("%s: %s\n", func, PMIx_Error_string(ret));                          \
    if (ret != PMIX_SUCCESS) return -1;                                        \
} while(0)

int main(int argc, char **argv) {

	pmix_status_t status;
	pmix_proc_t proc;

	sleep(30);

	status = PMIx_Init(&proc, NULL, 0);
	CHECK_ERROR(status, "PMIx_Init()");
	printf("PMIx_Init() succeeded: nspace '%s' rank %d\n", proc.nspace,
		proc.rank);

	if (!PMIx_Initialized()) {
	    printf("PMIx_Initialized() returned false.\n");
	    return -1;
	}
	printf("PMIx_Initialized() returned true.\n");

	sleep(2);

	pmix_value_t val;
	PMIX_VAL_ASSIGN(&val, string, "My test value");
	//PMIX_VAL_SET(&val, string, "My test value");
	status = PMIx_Put(PMIX_GLOBAL, "MyStringKey", &val);
	CHECK_ERROR(status, "PMIx_Put(MyStringKey)");

	sleep(2);

	PMIX_VAL_SET(&val, int, 42);
	status = PMIx_Put(PMIX_GLOBAL, "MyIntKey", &val);
	CHECK_ERROR(status, "PMIx_Put(MyIntKey)");

	sleep(2);

	status = PMIx_Commit();
	CHECK_ERROR(status, "PMIx_Commit()");

	sleep(2);

	pmix_value_t *retval;
	status = PMIx_Get(&proc, "MyStringKey", NULL, 0, &retval);
	CHECK_ERROR(status, "PMIx_Get(MyStringKey)");
	if (retval == NULL) {
	    printf("PMIx_Get(MyStringKey) failed: retval == NULL\n");
	    return -1;
	}
	if (retval->type != PMIX_VAL_TYPE_string) {
	    printf("PMIx_Get(MyStringKey) failed: type == %s\n",
		    PMIx_Data_type_string(retval->type));
	    return -1;
	}
	printf("PMIx_Get(MyStringKey) succeeded: value '%s'\n",
		PMIX_VAL_FIELD_string(retval));

	sleep(2);

	status = PMIx_Get(&proc, "MyIntKey", NULL, 0, &retval);
	CHECK_ERROR(status, "PMIx_Get(MyIntKey)");
	if (retval == NULL) {
	    printf("PMIx_Get(MyIntKey) failed: retval == NULL\n");
	    return -1;
	}
	if (retval->type != PMIX_VAL_TYPE_int) {
	    printf("PMIx_Get(MyIntKey) failed: type == %s\n",
		    PMIx_Data_type_string(retval->type));
	    return -1;
	}
	printf("PMIx_Get(MyIntKey) succeeded: value %d\n",
		PMIX_VAL_FIELD_int(retval));


	sleep(2);

	status = PMIx_Finalize(NULL, 0);
	CHECK_ERROR(status, "PMIx_Finalize()");

    return 0;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/

