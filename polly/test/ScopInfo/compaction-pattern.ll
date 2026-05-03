; RUN: opt %loadNPMPolly -polly-process-unprofitable -polly-allow-nonaffine -polly-detect-compaction-patterns '-passes=print<polly-function-scops>' -disable-output < %s 2>&1 | FileCheck %s

target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128"

define void @compact_only(ptr noalias %A, ptr noalias %B, ptr %out_n, i64 %N) {
entry:
  %cmp.entry = icmp sgt i64 %N, 0
  br i1 %cmp.entry, label %for, label %exit

for:
  %i = phi i64 [ 0, %entry ], [ %i.next, %latch ]
  %pos = phi i64 [ 0, %entry ], [ %pos.next, %latch ]
  %src = getelementptr inbounds i32, ptr %A, i64 %i
  %x = load i32, ptr %src, align 4
  %pred = icmp sgt i32 %x, 10
  br i1 %pred, label %then, label %latch

then:
  %dst = getelementptr inbounds i32, ptr %B, i64 %pos
  store i32 %x, ptr %dst, align 4
  %pos.then = add nsw i64 %pos, 1
  br label %latch

latch:
  %pos.next = phi i64 [ %pos.then, %then ], [ %pos, %for ]
  %i.next = add nsw i64 %i, 1
  %cmp.exit = icmp slt i64 %i.next, %N
  br i1 %cmp.exit, label %for, label %exit

exit:
  %pos.out = phi i64 [ 0, %entry ], [ %pos.next, %latch ]
  store i64 %pos.out, ptr %out_n, align 8
  ret void
}

; CHECK:      Statements {
; CHECK-NEXT: 	Stmt_for__TO__latch
; CHECK-NEXT:             Domain :=
; CHECK:                  Compaction Pattern :=	copy-filter-like
; CHECK:                  ReadAccess :=	[Reduction Type: NONE] [Scalar: 0]
; CHECK-NEXT:                 [N] -> { Stmt_for__TO__latch[i0] -> MemRef_A[i0] };
; CHECK:                  MayWriteAccess :=	[Reduction Type: NONE] [Scalar: 0]
; CHECK-NEXT:                 [N] -> { Stmt_for__TO__latch[i0] -> MemRef_B[o0] };
