/*
 * ParaStation
 *
 * Copyright (C) 2011-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <errno.h>

#include "list.h"
#include "psidutil.h"

#include "psidhook.h"

/** Structure used to create actual function lists */
typedef struct {
    PSIDhook_func_t * func; /**< Hook functions to be added to the list */
    list_t next;            /**< Actual list entry */
} hook_ref_t;

/** Array used to store list-heads */
static list_t *hookTable = NULL;


/** List of unused plugin-references */
static LIST_HEAD(refFreeList);

/** Chunk size for allocation new references via malloc() */
#define REF_CHUNK 128

/**
 * @brief Get new reference
 *
 * Provide a new and empty function-reference. References are taken
 * from @ref refFreeList as long as it provides free references. If no
 * reference is available from @ref refFreeList, a new chunk of @ref
 * REF_CHUNK references is allocated via malloc().
 *
 * @return On success a pointer to the reference is returned. If
 * malloc() fails, NULL is returned.
 */
static hook_ref_t * getRef(void){
    hook_ref_t *ref;

    if (list_empty(&refFreeList)) {
	hook_ref_t *refs = malloc(REF_CHUNK*sizeof(*refs));
	unsigned int i;

	if (!refs) return NULL;

	for (i=0; i<REF_CHUNK; i++) {
	    refs[i].func = NULL;
	    list_add_tail(&refs[i].next, &refFreeList);
	}
    }

    /* get list's first usable element */
    ref = list_entry(refFreeList.next, hook_ref_t, next);
    list_del(&ref->next);
    INIT_LIST_HEAD(&ref->next);

    return ref;
}

/**
 * @brief Put reference
 *
 * Put a function-reference no longer used back into @ref
 * refFreeList. From here the reference might be re-used via @ref
 * getRef().
 *
 * @param ref The reference to be put back.
 *
 * @return No return value.
 */
static void putRef(hook_ref_t *ref) {
    ref->func = NULL;
    list_add_tail(&ref->next, &refFreeList);
}

/**
 * @brief Find reference
 *
 * Find the function-reference to the hook-function @a func in the
 * list of references @a refList.
 *
 * @param refList The reference list to search in
 *
 * @param func The hook-function to search for
 *
 * @return Return the reference to the function @a func, if it is
 * found within the list. Otherwise NULL is given back.
 */
static hook_ref_t * findRef(list_t *refList, PSIDhook_func_t *func)
{
    list_t *t;

    if (!refList || !func) return NULL;

    list_for_each(t, refList) {
	hook_ref_t *ref = list_entry(t, hook_ref_t, next);

	if (ref->func == func) return ref;
    }

    return NULL;
}

static void initHookTable(const char *caller)
{
    if (hookTable) {
	PSID_log(-1, "%s: hook table already initialized!", __func__);
	PSID_log(-1, " Called by %s\n", caller);
	return;
    }

    hookTable = malloc(PSIDHOOK_LAST * sizeof(*hookTable));

    if (!hookTable) PSID_exit(errno, "%s", __func__);

    unsigned int h;
    for (h=0; h<PSIDHOOK_LAST; h++) INIT_LIST_HEAD(&hookTable[h]);
}

static inline int checkHook(PSIDhook_t hook)
{
    if (hook >= PSIDHOOK_LAST || !hookTable) return 0;

    return 1;
}

bool PSIDhook_add(PSIDhook_t hook, PSIDhook_func_t func)
{
    hook_ref_t *ref;

    if (!hookTable) initHookTable(__func__);

    if (!checkHook(hook)) {
	PSID_log(-1, "%s: unkown hook %d\n", __func__, hook);
	return false;
    }

    if (findRef(&hookTable[hook], func)) {
	PSID_log(-1, "%s: Function %p already registered on hook %d\n",
		 __func__, func, hook);
	return false;
    }

    ref = getRef();
    if (!ref) {
	PSID_warn(-1, errno, "%s", __func__);
	return false;
    }

    ref->func = func;
    list_add_tail(&ref->next, &hookTable[hook]);

    return true;
}

bool PSIDhook_del(PSIDhook_t hook, PSIDhook_func_t func)
{
    hook_ref_t *ref;

    if (!hookTable) {
	PSID_log(-1, "%s: hook facility uninitialized\n", __func__);
	return false;
    }

    if (!checkHook(hook)) {
	PSID_log(-1, "%s: unkown hook %d\n", __func__, hook);
	return false;
    }

    ref = findRef(&hookTable[hook], func);

    if (!ref) {
	PSID_log(-1, "%s: Function %p not found on hook %d\n",
		 __func__, func, hook);
	return false;
    }

    list_del(&ref->next);
    putRef(ref);

    return true;
}

int PSIDhook_call(PSIDhook_t hook, void *arg)
{
    list_t *h, *tmp;
    int ret = PSIDHOOK_NOFUNC;

    if (!hookTable) return 0;

    if (!checkHook(hook)) {
	PSID_log(-1, "%s: unkown hook %d\n", __func__, hook);
	return -1;
    }

    list_for_each_safe(h, tmp, &hookTable[hook]) {
	hook_ref_t *ref = list_entry(h, hook_ref_t, next);

	if (ref->func) {
	    int fret = ref->func(arg);

	    if (fret < ret) ret = fret;
	}
    }

    return ret;
}

void initHooks(void)
{
    if (!hookTable) initHookTable(__func__);
}
