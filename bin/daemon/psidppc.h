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
 * Utilities for SMT detection on Power
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDPOWER_H
#define __PSIDPOWER_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Test for Power CPU
 *
 * Test for Power/PPC Processor. Just test the architecture the code
 * is created for.
 *
 * @return Returns 1 if the processor is Power or PPC, or otherwise 0.
 */
int PSID_PPC(void);

/**
 * @brief Get number of physical CPUs on Power.
 *
 * Determine the number of physical CPUs. The number of physical CPUs
 * might differ from the number of virtual CPUs e.g. on Cell which
 * supports the SMT Technology.
 *
 * This function only supports the Power platform. For other
 * platforms, similar functionality has to be implemented. Be aware of
 * the fact that detecting the underlying hardware structure is
 * massively hardware dependant.
 *
 * @return On success, the number of physical CPUs is returned. If an
 * error occurred, e.g. the current platform is not Power, the number
 * of virtual CPUs is returned.
 */
long PSID_getPhysCPUs_PPC(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDPOWER_H */
