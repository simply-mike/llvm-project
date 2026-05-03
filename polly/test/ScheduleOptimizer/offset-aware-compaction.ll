; RUN: opt %loadNPMPolly -polly-process-unprofitable -polly-allow-nonaffine -polly-pattern-matching-based-opts=false -polly-postopts=0 -polly-force-offset-fusion=1 "-passes=scop(polly-opt-isl,print<polly-opt-isl>)" -disable-output < %s | FileCheck %s
; RUN: opt %loadNPMPolly -polly-process-unprofitable -polly-allow-nonaffine -polly-force-offset-fusion=1 -polly-detect-compaction-patterns "-passes=print<polly-function-scops>" -disable-output < %s 2>&1 | FileCheck %s --check-prefix=SCOPS

target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128"

define void @offset_compaction(ptr noalias %A, ptr noalias %B, i64 %N) {
entry:
  br label %for.fill

for.fill:
  %i.fill = phi i64 [ 0, %entry ], [ %i.fill.next, %for.fill.inc ]
  %fill.cond = icmp slt i64 %i.fill, %N
  br i1 %fill.cond, label %for.fill.body, label %for.transform.preheader

for.fill.body:
  %fill.idx = getelementptr inbounds i32, ptr %A, i64 %i.fill
  store i32 0, ptr %fill.idx, align 4
  br label %for.fill.inc

for.fill.inc:
  %i.fill.next = add nsw i64 %i.fill, 1
  br label %for.fill

for.transform.preheader:
  br label %for.transform

for.transform:
  %i.transform = phi i64 [ 1, %for.transform.preheader ], [ %i.transform.next, %for.transform.inc ]
  %transform.cond = icmp slt i64 %i.transform, %N
  br i1 %transform.cond, label %for.transform.body, label %for.copy.preheader

for.transform.body:
  %transform.idx = getelementptr inbounds i32, ptr %A, i64 %i.transform
  %transform.old = load i32, ptr %transform.idx, align 4
  %transform.mul = shl nsw i32 %transform.old, 1
  %transform.add = add nsw i32 %transform.mul, 1
  store i32 %transform.add, ptr %transform.idx, align 4
  br label %for.transform.inc

for.transform.inc:
  %i.transform.next = add nsw i64 %i.transform, 1
  br label %for.transform

for.copy.preheader:
  %n.minus.1 = add nsw i64 %N, -1
  br label %for.copy

for.copy:
  %i.copy = phi i64 [ 0, %for.copy.preheader ], [ %i.copy.next, %for.copy.latch ]
  %pos.copy = phi i64 [ 0, %for.copy.preheader ], [ %pos.next, %for.copy.latch ]
  %copy.cond = icmp slt i64 %i.copy, %n.minus.1
  br i1 %copy.cond, label %for.copy.body, label %exit

for.copy.body:
  %copy.src = getelementptr inbounds i32, ptr %A, i64 %i.copy
  %copy.val = load i32, ptr %copy.src, align 4
  %copy.pred = icmp sgt i32 %copy.val, 10
  br i1 %copy.pred, label %for.copy.then, label %for.copy.latch

for.copy.then:
  %copy.dst = getelementptr inbounds i32, ptr %B, i64 %pos.copy
  store i32 %copy.val, ptr %copy.dst, align 4
  %pos.inc = add nsw i64 %pos.copy, 1
  br label %for.copy.latch

for.copy.latch:
  %pos.next = phi i64 [ %pos.inc, %for.copy.then ], [ %pos.copy, %for.copy.body ]
  %i.copy.next = add nsw i64 %i.copy, 1
  br label %for.copy

exit:
  ret void
}

; CHECK:      Calculated schedule:
; CHECK-NEXT: domain: "[N] -> { Stmt_for_copy_preheader[]; Stmt_for_copy[i0] : 0 <= i0 < N; Stmt_for_copy[0] : N <= 0; Stmt_for_transform_body[i0] : 0 <= i0 <= -2 + N; Stmt_for_copy_latch[i0] : 0 <= i0 <= -2 + N; Stmt_for_copy_body__TO__for_copy_latch[i0] : 0 <= i0 <= -2 + N; Stmt_for_fill_body[i0] : 0 <= i0 < N }"
; CHECK-NEXT: child:
; CHECK-NEXT:   sequence:
; CHECK-NEXT:   - filter: "[N] -> { Stmt_for_copy_preheader[] }"
; CHECK-NEXT:   - filter: "[N] -> { Stmt_for_copy_latch[i0]; Stmt_for_copy_body__TO__for_copy_latch[i0]; Stmt_for_transform_body[i0]; Stmt_for_copy[i0]; Stmt_for_fill_body[i0] }"
; CHECK-NEXT:     child:
; CHECK-NEXT:       schedule: "[N] -> [{ Stmt_for_copy_latch[i0] -> [(i0)]; Stmt_for_copy_body__TO__for_copy_latch[i0] -> [(i0)]; Stmt_for_transform_body[i0] -> [(1 + i0)]; Stmt_for_copy[i0] -> [(i0)]; Stmt_for_fill_body[i0] -> [(i0)] }]"
; CHECK-NEXT:       child:
; CHECK-NEXT:         sequence:
; CHECK-NEXT:         - filter: "[N] -> { Stmt_for_fill_body[i0] }"
; CHECK-NEXT:         - filter: "[N] -> { Stmt_for_transform_body[i0] }"
; CHECK-NEXT:         - filter: "[N] -> { Stmt_for_copy[i0] }"
; CHECK-NEXT:         - filter: "[N] -> { Stmt_for_copy_body__TO__for_copy_latch[i0] }"
; CHECK-NEXT:         - filter: "[N] -> { Stmt_for_copy_latch[i0] }"

; SCOPS:      Stmt_for_transform_body
; SCOPS-NEXT:             Domain :=
; SCOPS-NEXT:                 [N] -> { Stmt_for_transform_body[i0] : 0 <= i0 <= -2 + N };
; SCOPS-NEXT:             Logical Domain :=
; SCOPS-NEXT:                 [N] -> { Stmt_for_transform_body[i0] : 0 < i0 < N };
; SCOPS:      Stmt_for_copy_body__TO__for_copy_latch
; SCOPS-NEXT:             Domain :=
; SCOPS:                  Compaction Pattern :=	copy-filter-like
