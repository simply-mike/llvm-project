; RUN: opt %loadNPMPolly -polly-process-unprofitable -polly-pattern-matching-based-opts=false -polly-postopts=0 -polly-force-offset-fusion=1 "-passes=scop(polly-opt-isl,print<polly-opt-isl>)" -disable-output < %s | FileCheck %s
; RUN: opt %loadNPMPolly -polly-process-unprofitable -polly-force-offset-fusion=1 "-passes=print<polly-function-scops>" -disable-output < %s 2>&1 | FileCheck %s --check-prefix=LOGICAL
; RUN: opt %loadNPMPolly -polly-process-unprofitable -polly-pattern-matching-based-opts=false -polly-postopts=0 -polly-force-offset-fusion=1 "-passes=scop(polly-opt-isl,print<polly-ast>)" -disable-output < %s | FileCheck %s --check-prefix=AST

define void @offset_fusion(ptr noalias nonnull %A, ptr noalias nonnull %B, i32 %n) {
entry:
  br label %for.fill

for.fill:
  %i.fill = phi i32 [ 0, %entry ], [ %i.fill.next, %for.fill.inc ]
  %fill.cond = icmp slt i32 %i.fill, %n
  br i1 %fill.cond, label %for.fill.body, label %for.transform.preheader

for.fill.body:
  %fill.idx = getelementptr inbounds i32, ptr %A, i32 %i.fill
  store i32 0, ptr %fill.idx, align 4
  br label %for.fill.inc

for.fill.inc:
  %i.fill.next = add nuw nsw i32 %i.fill, 1
  br label %for.fill

for.transform.preheader:
  br label %for.transform

for.transform:
  %i.transform = phi i32 [ 1, %for.transform.preheader ], [ %i.transform.next, %for.transform.inc ]
  %transform.cond = icmp slt i32 %i.transform, %n
  br i1 %transform.cond, label %for.transform.body, label %for.copy.preheader

for.transform.body:
  %transform.idx = getelementptr inbounds i32, ptr %A, i32 %i.transform
  %transform.old = load i32, ptr %transform.idx, align 4
  %transform.mul = shl nsw i32 %transform.old, 1
  %transform.add = add nsw i32 %transform.mul, 1
  store i32 %transform.add, ptr %transform.idx, align 4
  br label %for.transform.inc

for.transform.inc:
  %i.transform.next = add nuw nsw i32 %i.transform, 1
  br label %for.transform

for.copy.preheader:
  %n.minus.1 = add nsw i32 %n, -1
  br label %for.copy

for.copy:
  %i.copy = phi i32 [ 0, %for.copy.preheader ], [ %i.copy.next, %for.copy.inc ]
  %copy.cond = icmp slt i32 %i.copy, %n.minus.1
  br i1 %copy.cond, label %for.copy.body, label %exit

for.copy.body:
  %copy.src = getelementptr inbounds i32, ptr %A, i32 %i.copy
  %copy.val = load i32, ptr %copy.src, align 4
  %copy.dst = getelementptr inbounds i32, ptr %B, i32 %i.copy
  store i32 %copy.val, ptr %copy.dst, align 4
  br label %for.copy.inc

for.copy.inc:
  %i.copy.next = add nuw nsw i32 %i.copy, 1
  br label %for.copy

exit:
  ret void
}

; CHECK:      Calculated schedule:
; CHECK-NEXT: domain: "[n] -> { Stmt_for_copy_body[i0] : 0 <= i0 <= -2 + n; Stmt_for_transform_body[i0] : 0 <= i0 <= -2 + n; Stmt_for_fill_body[i0] : 0 <= i0 < n }"
; CHECK-NEXT: child:
; CHECK-NEXT:   schedule: "[n] -> [{ Stmt_for_copy_body[i0] -> [(i0)]; Stmt_for_transform_body[i0] -> [(1 + i0)]; Stmt_for_fill_body[i0] -> [(i0)] }]"
; CHECK-NEXT:   options: "{{.*}}isolate{{.*}}0 < i0 <= -2 + n{{.*}}atomic{{.*}}"
; CHECK-NEXT:   child:
; CHECK-NEXT:     sequence:
; CHECK-NEXT:     - filter: "[n] -> { Stmt_for_fill_body[i0] }"
; CHECK-NEXT:     - filter: "[n] -> { Stmt_for_transform_body[i0] }"
; CHECK-NEXT:     - filter: "[n] -> { Stmt_for_copy_body[i0] }"

; LOGICAL:      Stmt_for_transform_body
; LOGICAL-NEXT:            Domain :=
; LOGICAL-NEXT:                [n] -> { Stmt_for_transform_body[i0] : 0 <= i0 <= -2 + n };
; LOGICAL-NEXT:            Logical Domain :=
; LOGICAL-NEXT:                [n] -> { Stmt_for_transform_body[i0] : 0 < i0 < n };

; AST:      if (n >= 3) {
; AST-NEXT:   Stmt_for_fill_body(0);
; AST-NEXT:   Stmt_for_copy_body(0);
; AST-NEXT: }
; AST-NEXT: for (int c0 = 1; c0 < n - 1; c0 += 1) {
; AST-NEXT:   Stmt_for_fill_body(c0);
; AST-NEXT:   Stmt_for_transform_body(c0 - 1);
; AST-NEXT:   Stmt_for_copy_body(c0);
; AST-NEXT: }
; AST-NEXT: if (n >= 3) {
; AST-NEXT:   Stmt_for_fill_body(n - 1);
; AST-NEXT:   Stmt_for_transform_body(n - 2);
; AST-NEXT: }
