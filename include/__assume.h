/*
 * ParaStation
 *
 * Copyright (C) 2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef ___ASSUME_H_
#define ___ASSUME_H_

/* HACK Provide hints according to compilers' capabilities */
inline void ASSUME(bool cond) {
#if defined(__clang__)  // Must go first because Clang also defines __GNUC__.
  __builtin_assume(cond);
#elif defined(__GNUC__)
  if (!cond) { __builtin_unreachable(); }
#else
  // Do nothing.
#endif
}

#endif /* ___ASSUME_H_ */
