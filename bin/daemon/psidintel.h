/*
 * ParaStation
 *
 * Copyright (C) 2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * \file
 * Utilities for SMT detection on Intel IA32
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDINTEL_H
#define __PSIDINTEL_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Test for Intel CPU
 *
 * Test for Genuine Intel Processor. Use the CPUID instruction to get
 * magic string identifying the CPU to be made by Intel.
 *
 * @return Returns 1 if the processor is Genuine Intel, or otherwise
 * 0.
 */
int PSID_GenuineIntel(void);

/**
 * @brief Get number of physical CPUs on Intel IA-32.
 *
 * Determine the number of physical CPUs. The number of physical CPUs
 * might differ from the number of virtual CPUs e.g. on newer Pentium
 * platforms which support the Hyper-Threading Technology.
 *
 * This function only supports the Intel IA32 platform. For other
 * platforms, similar functionality has to be implemented. Be aware of
 * the fact that detecting the underlying hardware structure is
 * massively hardware dependant.
 *
 * @return On success, the number of physical CPUs is returned. If an
 * error occurred, e.g. the current platform is not Genuine Intel, the
 * number of virtual CPUs is returned.
 */
long PSID_getPhysCPUs_IA32(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDINTEL_H */
