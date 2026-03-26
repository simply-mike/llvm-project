; RUN: opt %loadNPMPolly -polly-process-unprofitable -polly-allow-nonaffine -polly-pattern-matching-based-opts=false -polly-postopts=0 -polly-force-offset-fusion=1 "-passes=scop(polly-opt-isl,print<polly-opt-isl>)" -disable-output < %s | FileCheck %s
; RUN: opt %loadNPMPolly -polly-process-unprofitable -polly-allow-nonaffine -polly-force-offset-fusion=1 -polly-detect-compaction-patterns "-passes=print<polly-function-scops>" -disable-output < %s | FileCheck %s --check-prefix=SCOPS

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
; CHECK-NEXT: domain: "[p_0] -> { Stmt8[i0] : 0 <= i0 < p_0; Stmt8[0] : p_0 <= 0; Stmt7[]; Stmt9[i0] : 0 <= i0 <= -2 + p_0; Stmt1[i0] : 0 <= i0 < p_0; Stmt5[i0] : 0 <= i0 <= -2 + p_0; Stmt10[i0] : 0 <= i0 <= -2 + p_0 }"
; CHECK-NEXT: child:
; CHECK-NEXT:   sequence:
; CHECK-NEXT:   - filter: "[p_0] -> { Stmt7[] }"
; CHECK-NEXT:     child:
; CHECK-NEXT:   - filter: "[p_0] -> { Stmt1[i0]; Stmt9[i0]; Stmt10[i0]; Stmt5[i0]; Stmt8[i0] }"
; CHECK-NEXT:     child:
; CHECK-NEXT:       schedule: "[p_0] -> [{ Stmt1[i0] -> [(i0)]; Stmt9[i0] -> [(i0)]; Stmt10[i0] -> [(i0)]; Stmt5[i0] -> [(1 + i0)]; Stmt8[i0] -> [(i0)] }]"
; CHECK-NEXT:       child:
; CHECK-NEXT:         sequence:
; CHECK-NEXT:         - filter: "[p_0] -> { Stmt1[i0] }"
; CHECK-NEXT:         - filter: "[p_0] -> { Stmt5[i0] }"
; CHECK-NEXT:         - filter: "[p_0] -> { Stmt8[i0] }"
; CHECK-NEXT:         - filter: "[p_0] -> { Stmt9[i0] }"
; CHECK-NEXT:         - filter: "[p_0] -> { Stmt10[i0] }"

; SCOPS:      Stmt5
; SCOPS-NEXT:             Domain :=
; SCOPS-NEXT:                 [p_0] -> { Stmt5[i0] : 0 <= i0 <= -2 + p_0 };
; SCOPS-NEXT:             Logical Domain :=
; SCOPS-NEXT:                 [p_0] -> { Stmt5[i0] : 0 < i0 < p_0 };
; SCOPS:      Stmt9
; SCOPS-NEXT:             Domain :=
; SCOPS:                  Compaction Pattern :=	copy-filter-like
