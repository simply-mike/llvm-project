; RUN: opt %loadNPMPolly -polly-process-unprofitable '-passes=print<polly-function-scops>' -disable-output < %s 2>&1 | FileCheck %s

target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64"

define void @inttoptr_phi_iterator(ptr noalias %base, i64 %n) {
entry:
  %enough = icmp ugt i64 %n, 1
  br i1 %enough, label %preheader, label %exit

preheader:
  %base.int = ptrtoint ptr %base to i64
  %start.int = add i64 %base.int, 4
  %start.ptr = inttoptr i64 %start.int to ptr
  %end.ptr = getelementptr inbounds i32, ptr %base, i64 %n
  br label %loop

loop:
  %ptr.iv = phi ptr [ %start.ptr, %preheader ], [ %ptr.next, %loop ]
  %val = load i32, ptr %ptr.iv, align 4
  %mul = shl nsw i32 %val, 1
  %add = add nsw i32 %mul, 1
  store i32 %add, ptr %ptr.iv, align 4
  %ptr.next = getelementptr i8, ptr %ptr.iv, i64 4
  %done = icmp eq ptr %ptr.next, %end.ptr
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; CHECK:      Function: inttoptr_phi_iterator
; CHECK-NEXT: Region: %loop---%exit
; CHECK:      Statements {
; CHECK-NEXT: 	Stmt_loop
; CHECK-NEXT:             Domain :=
; CHECK-NEXT:                 [base, n] -> { Stmt_loop[i0] : 0 <= i0 <= -2 + n };
; CHECK:                  ReadAccess :=	[Reduction Type: NONE] [Scalar: 0]
; CHECK-NEXT:                 [base, n] -> { Stmt_loop[i0] -> MemRef_start_ptr[i0] };
; CHECK:                  MustWriteAccess :=	[Reduction Type: NONE] [Scalar: 0]
; CHECK-NEXT:                 [base, n] -> { Stmt_loop[i0] -> MemRef_start_ptr[i0] };
