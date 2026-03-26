//===--------------- polly/Options.h - The Polly option category *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Introduce an option category for Polly.
//
//===----------------------------------------------------------------------===//

#ifndef POLLY_OPTIONS_H
#define POLLY_OPTIONS_H

#include "llvm/Support/CommandLine.h"

extern llvm::cl::OptionCategory PollyCategory;

namespace polly {
/// Enable aggressive offset-aware fusion heuristics in the scheduler.
///
/// This is intentionally declared in the shared Polly options header because
/// multiple scheduling components may want to consult the same gate as we grow
/// the fusion pipeline.
extern llvm::cl::opt<bool> PollyForceOffsetFusion;
extern llvm::cl::opt<bool> PollyDetectCompactionPatterns;
} // namespace polly

#endif
