#include <pmix.h>
#include <stdio.h>

/* Test for PMIx_Query_info (needs min PMIx 4) */

pmix_proc_t proc;

#define CHECK_PMIX_ERR(pmix_errno, func_name) \
    if (pmix_errno != PMIX_SUCCESS) { \
        printf("[%s:%u]: Error on %s: %s\n", proc.nspace, proc.rank, \
               func_name, PMIx_Error_string(pmix_errno)); \
        goto fn_fail; \
    }

static
int query_nspace_info( bool refresh, char ***nspaces, size_t *nnspaces)
{
    pmix_status_t rc = PMIX_SUCCESS;
    pmix_query_t *query = NULL;
    pmix_info_t *info = NULL;
    size_t ninfo = 0;
    size_t nspace_counter = 0;
    char **ns = NULL;

    PMIX_QUERY_CREATE(query, 1);
    PMIX_ARGV_APPEND(rc, query->keys, PMIX_QUERY_NAMESPACE_INFO);
    CHECK_PMIX_ERR(rc, "PMIX_ARGV_APPEND");

    PMIX_QUERY_QUALIFIERS_CREATE(query, 1);
    PMIX_INFO_LOAD(&(query->qualifiers[0]), PMIX_QUERY_REFRESH_CACHE, &refresh, PMIX_BOOL);
    rc = PMIx_Query_info(query, 1, &info, &ninfo);
    CHECK_PMIX_ERR(rc, "PMIx_Query_info");

    /* Create list of namespaces */
    for (size_t n = 0; n < ninfo; n++) {
        if (proc.rank == 0) {
            printf("[%s:%u]: nspace info %ld:\n %s\n", proc.nspace, proc.rank, n, PMIx_Info_string(&info[n]));
        }
        if (PMIX_CHECK_KEY(&info[n], PMIX_QUERY_NAMESPACE_INFO)) {
            pmix_info_t *nsinfo_array = (pmix_info_t *) info[n].value.data.darray->array;
            nspace_counter = info[n].value.data.darray->size;
            if (nspace_counter <= 0) {
                printf("[%s:%u]: PMIX_QUERY_NAMESPACE_INFO returned invalid number of nspaces: %ld\n",
                       proc.nspace, proc.rank, nspace_counter);
                goto fn_fail;
            }
            ns = malloc(nspace_counter * sizeof(char *));
            for (size_t k = 0; k < nspace_counter; k++) {
                /* nsinfo is PMIX_JOB_INFO_ARRAY for prrte */
                pmix_info_t *nsinfo = (pmix_info_t *) nsinfo_array[k].value.data.darray->array;
                size_t nsinfo_size = nsinfo_array[k].value.data.darray->size;
                if (proc.rank == 0) {
                    printf("[%s:%u]: obtained %ld infos about nspace at index %ld\n",
                           proc.nspace, proc.rank, nsinfo_size, k);
                }
                for (size_t j = 0; j < nsinfo_size; j++) {
                    printf("[%s:%u]: obtained key %s for nspace at index %ld\n",
                           proc.nspace, proc.rank, nsinfo[j].key, k);
                    if (PMIX_CHECK_KEY(&nsinfo[j], PMIX_NSPACE)) {
                        ns[k] = malloc(PMIX_MAX_NSLEN + 1);
                        strncpy(ns[k], nsinfo[j].value.data.string, PMIX_MAX_NSLEN);
                    }
                }
            }
        }
    }
    PMIX_QUERY_FREE(query, 1);
    PMIX_INFO_FREE(info, ninfo);

    *nnspaces = nspace_counter;
    *nspaces = ns;

  fn_exit:
    return rc;
  fn_fail:
    if (info) {
        PMIX_INFO_FREE(info, ninfo);
    }
    if (query) {
        PMIX_QUERY_FREE(query, 1);
    }
    goto fn_exit;
}

static
int query_proc_table(bool refresh, char **nspaces, int nnspaces, size_t * nprocs)
{
    pmix_status_t rc = PMIX_SUCCESS;
    pmix_query_t *query = NULL;
    pmix_info_t *info = NULL;
    size_t ninfo = 0;
    size_t proc_counter = 0;

    PMIX_QUERY_CREATE(query, nnspaces);
    for (int i = 0; i < nnspaces; i++) {
        PMIX_ARGV_APPEND(rc, query[i].keys, PMIX_QUERY_PROC_TABLE);
        CHECK_PMIX_ERR(rc, "PMIX_ARGV_APPEND");

        /* Set required namespace qualifier for PMIX_QUERY_PROC_TABLE */
        PMIX_QUERY_QUALIFIERS_CREATE(&(query[i]), 2);
        PMIX_INFO_LOAD(&(query[i].qualifiers[0]), PMIX_NSPACE, nspaces[i], PMIX_STRING);
        PMIX_INFO_LOAD(&(query[i].qualifiers[1]), PMIX_QUERY_REFRESH_CACHE, &refresh, PMIX_BOOL);
        rc = PMIx_Query_info(&(query[i]), 1, &info, &ninfo);
        CHECK_PMIX_ERR(rc, "PMIx_Query_info");

        for (size_t n = 0; n < ninfo; n++) {
            if (proc.rank == 0) {
                printf("[%s:%u]: Proc table %ld:\n %s\n", proc.nspace, proc.rank, n, PMIx_Info_string(&info[n]));
            }
            if (PMIX_CHECK_KEY(&(info[n]), PMIX_QUERY_PROC_TABLE)) {
                /* Determine number of active processes, exclude terminated */
                pmix_proc_info_t *proc_infos = info[n].value.data.darray->array;
                for (size_t k = 0; k < info[n].value.data.darray->size; k++) {
                    if ((proc_infos[k].state < PMIX_PROC_STATE_UNTERMINATED) &&
                        (proc_infos[k].state != PMIX_PROC_STATE_UNDEF)) {
                        proc_counter++;
                    }
                }
                break;
            }
        }

        /* Prepare for use in next iteration */
        PMIX_INFO_FREE(info, ninfo);
        info = NULL;
        ninfo = 0;
    }
    PMIX_QUERY_FREE(query, nnspaces);

    *nprocs = proc_counter;
  fn_exit:
    return rc;
  fn_fail:
    if (info) {
        PMIX_INFO_FREE(info, ninfo);
    }
    if (query) {
        PMIX_QUERY_FREE(query, nnspaces);
    }
    goto fn_exit;
}

int main(int argc, char **argv)
{
    pmix_status_t rc;
    pmix_value_t *val = NULL;
    char **nspaces = NULL;
    size_t nnspaces;
    size_t nprocs;
    pmix_proc_t wproc;
    int job_size;
    bool refresh = true;

    rc = PMIx_Init(&proc, NULL, 0);
    CHECK_PMIX_ERR(rc, "PMIx_Init");

    printf("[%s:%u]: Running\n", proc.nspace, proc.rank);

    /* PMIx_Query_info (blocking version) has been introduced in PMIx 4 */
    if (PMIX_VERSION_MAJOR < 4) {
        printf("[%s:%u]: Need at least PMIx 4!\n", proc.nspace, proc.rank);
        goto fn_exit;
    }

    /* Get job size */
    PMIX_PROC_CONSTRUCT(&wproc);
    PMIX_LOAD_PROCID(&wproc, proc.nspace, PMIX_RANK_WILDCARD);
    rc = PMIx_Get(&wproc, PMIX_JOB_SIZE, NULL, 0, &val);
    CHECK_PMIX_ERR(rc, "PMIx_Get");
    job_size = val->data.uint32;
    PMIX_VALUE_RELEASE(val);

    /* Need at least 2 procs */
    if (job_size < 2) {
        printf("[%s:%u]: Need at least 2 procs!\n", proc.nspace, proc.rank);
        goto fn_exit;
    }

    /* Query all nspaces */
    rc = query_nspace_info(refresh, &nspaces, &nnspaces);
    if (rc != PMIX_SUCCESS) {
        goto fn_fail;
    }

    /* Query proc tables for all nspaces */
    rc = query_proc_table(refresh, nspaces, nnspaces, &nprocs);
    if (rc != PMIX_SUCCESS) {
        goto fn_fail;
    }

    /* Sanity check */
    if (nprocs != job_size) {
        printf("[%s:%u]: Number of active procs in proc table does not match job size, job size: %d, active procs: %ld \n",
               proc.nspace, proc.rank, job_size, nprocs);
        goto fn_fail;
    } else {
        printf("[%s:%u]: %ld active procs in proc table\n", proc.nspace, proc.rank, nprocs);
    }

    /* rank 1 exit, others sleep for a bit so that rank 1 can exit in the meantime */
    if (proc.rank == 1) {
        printf("[%s:%u]: Exit\n", proc.nspace, proc.rank);
        rc = 0;
        goto fn_exit;
    } else {
        printf("[%s:%u]: Sleep\n", proc.nspace, proc.rank);
        sleep(1);
    }

    /* Query proc tables again to test if exited proc is detected correctly */
    rc = query_proc_table(refresh, nspaces, nnspaces, &nprocs);
    if (rc != PMIX_SUCCESS) {
        goto fn_fail;
    }

    /* Sanity check */
    if (job_size != nprocs + 1) {
        printf("[%s:%u]: Number of procs does not match, expect %d, have %ld\n",
               proc.nspace, proc.rank, job_size - 1, nprocs);
        goto fn_fail;
    } else {
        printf("[%s:%u]: after shrink: %ld active procs in proc table\n",
               proc.nspace, proc.rank, nprocs);
    }

    /* TODO: Add a spawn here to test queries with more than one nspace */

  fn_exit:
    rc = PMIx_Finalize(NULL, 0);
    CHECK_PMIX_ERR(rc, "PMIx_finalize");
    printf("[%s:%u]: Exit\n", proc.nspace, proc.rank);

    if (nspaces) {
        for (int i = 0; i < nnspaces; i++) {
            free(nspaces[i]);
        }
        free(nspaces);
    }
    return (rc);
  fn_fail:
    printf("[%s:%u]: ERROR\n", proc.nspace, proc.rank);
    goto fn_exit;
}
