/*
 * ParaStation
 *
 * Copyright (C) 2011-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
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

/** List of hooks obsoleted in the meantime; trying to add a function
 * to one of these hooks will fail. */
PSIDhook_t obsoleteHooks[] = { PSIDHOOK_FRWRD_CINFO,
			       PSIDHOOK_FRWRD_KVS,
			       PSIDHOOK_FRWRD_SPAWNRES,
			       PSIDHOOK_FRWRD_CC_ERROR,
			       PSIDHOOK_FRWRD_DSOCK,
			       0 /* end of array */ };

/** Structure used to create actual function lists */
typedef struct {
    PSIDhook_func_t * func; /**< Hook functions to be added to the list */
    list_t next;            /**< Actual list entry */
} hook_ref_t;

/** Array used to store list-heads */
static struct {
    list_t list;    /**< list of hook references added to this hook */
    bool obsolete;  /**< flag obsolete hook; initialized from obsoleteHooks */
} hookTab[PSIDHOOK_LAST];

/** Flag to mark module's initialization status */
static bool hookTabInitialized = false;

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
    if (list_empty(&refFreeList)) {
	hook_ref_t *refs = malloc(REF_CHUNK * sizeof(*refs));
	if (!refs) return NULL;

	for (int i = 0; i < REF_CHUNK; i++) {
	    refs[i].func = NULL;
	    list_add_tail(&refs[i].next, &refFreeList);
	}
    }

    /* get list's first usable element */
    hook_ref_t *ref = list_entry(refFreeList.next, hook_ref_t, next);
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
    if (!refList || !func) return NULL;

    list_t *r;
    list_for_each(r, refList) {
	hook_ref_t *ref = list_entry(r, hook_ref_t, next);

	if (ref->func == func) return ref;
    }

    return NULL;
}

static void initHookTable(const char *caller)
{
    if (hookTabInitialized) {
	PSID_flog("hook table already initialized! Caller is %s\n", caller);
	return;
    }

    for (int h = 0; h < PSIDHOOK_LAST; h++) {
	INIT_LIST_HEAD(&hookTab[h].list);
	hookTab[h].obsolete = false;
    }

    for (int h = 0; obsoleteHooks[h]; h++) {
	hookTab[obsoleteHooks[h]].obsolete = true;
    }

    hookTabInitialized = true;
}

static inline bool checkHook(PSIDhook_t hook)
{
    if (hook < 0 || hook >= PSIDHOOK_LAST || !hookTabInitialized) return false;
    return true;
}

bool PSIDhook_add(PSIDhook_t hook, PSIDhook_func_t func)
{
    if (!hookTabInitialized) initHookTable(__func__);

    if (!checkHook(hook)) {
	PSID_flog("unknown hook %d\n", hook);
	return false;
    }

    if (hookTab[hook].obsolete) {
	PSID_flog("hook %d is marked as obsolete\n", hook);
	return false;
    }

    if (findRef(&hookTab[hook].list, func)) {
	PSID_flog("function %p already registered on hook %d\n", func, hook);
	return false;
    }

    hook_ref_t *ref = getRef();
    if (!ref) {
	PSID_fwarn(errno, "getRef()");
	return false;
    }

    ref->func = func;
    list_add_tail(&ref->next, &hookTab[hook].list);

    return true;
}

bool PSIDhook_del(PSIDhook_t hook, PSIDhook_func_t func)
{
    if (!hookTabInitialized) {
	PSID_flog("hook facility uninitialized\n");
	return false;
    }

    if (!checkHook(hook)) {
	PSID_flog("unknown hook %d\n", hook);
	return false;
    }

    hook_ref_t *ref = findRef(&hookTab[hook].list, func);
    if (!ref) {
	PSID_flog("function %p not found on hook %d\n", func, hook);
	return false;
    }

    list_del(&ref->next);
    putRef(ref);

    return true;
}

int PSIDhook_call(PSIDhook_t hook, void *arg)
{
    int ret = PSIDHOOK_NOFUNC;

    if (!hookTabInitialized) return 0;

    if (!checkHook(hook)) {
	PSID_flog("unknown hook %d\n", hook);
	return -1;
    }

    list_t *h, *tmp;
    list_for_each_safe(h, tmp, &hookTab[hook].list) {
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
    if (!hookTabInitialized) initHookTable(__func__);
}
