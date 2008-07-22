/*
 *               ParaStation
 *
 * Copyright (C) 2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * Utilities for SMT detection on AMD x86
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDAMD_H
#define __PSIDAMD_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Test for AMD CPU
 *
 * Test for Authentic AMD Processor. Use the CPUID instruction to get
 * magic string identifying the CPU to be made by AMD.
 *
 * @return Returns 1 if the processor is Authentic AMD, or otherwise
 * 0.
 */
int PSID_AuthenticAMD(void);

/**
 * @brief Get number of physical CPUs on Intel IA-32.
 *
 * Determine the number of physical CPUs. The number of physical CPUs
 * might differ from the number of virtual CPUs. Until now AMD has not
 * implemented the SMT Technology, thus the current implementation s
 * quite simple.
 *
 * This function only supports the AMD platform. For other platforms,
 * similar functionality has to be implemented. Be aware of the fact
 * that detecting the underlying hardware structure is massively
 * hardware dependant.
 *
 * @return On success, the number of physical CPUs is returned. If an
 * error occurred, e.g. the current platform is not Authentic AMD, the
 * number of virtual CPUs is returned.
 */
long PSID_getPhysCPUs_AMD(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDAMD_H */
