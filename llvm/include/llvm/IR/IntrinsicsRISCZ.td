//===- IntrinsicsRISCZ.td - Defines RISCZ intrinsics ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "riscz" in {

def int_riscz_flush
    : DefaultAttrsIntrinsic<[], [], [IntrNoMem, IntrHasSideEffects]>;

def int_riscz_rand : DefaultAttrsIntrinsic<[llvm_i64_ty], [], [IntrNoMem]>;

def int_riscz_quit_event
    : DefaultAttrsIntrinsic<[llvm_i64_ty], [], [IntrNoMem, IntrHasSideEffects]>;

def int_riscz_set_pixel
    : DefaultAttrsIntrinsic<[], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty],
                            [IntrNoMem, IntrHasSideEffects]>;

} // TargetPrefix = "riscz"
