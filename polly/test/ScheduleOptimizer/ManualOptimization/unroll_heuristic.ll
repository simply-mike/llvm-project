; RUN: opt %loadNPMPolly -polly-pragma-based-opts=1 '-passes=print<polly-opt-isl>' -disable-output < %s | FileCheck %s --check-prefix=PRAGMA
; RUN: opt %loadNPMPolly -polly-pragma-based-opts=0 '-passes=print<polly-opt-isl>' -disable-output < %s | FileCheck %s --check-prefix=NOPRAGMA
;
; Unrolling with heuristic factor.
; Currently not supported and expected to be handled by LLVM's unroll pass.
;
define void @func(i32 %n, ptr noalias nonnull %A) {
entry:
  br label %for

for:
  %j = phi i32 [0, %entry], [%j.inc, %inc]
  %j.cmp = icmp slt i32 %j, %n
  br i1 %j.cmp, label %body, label %exit

    body:
      store double 42.0, ptr %A
      br label %inc

inc:
  %j.inc = add nuw nsw i32 %j, 1
  br label %for, !llvm.loop !2

exit:
  br label %return

return:
  ret void
}


!2 = distinct !{!2, !4}
!4 = !{!"llvm.loop.unroll.enable", i1 true}


; PRAGMA-LABEL: Printing analysis 'Polly - Optimize schedule of SCoP' for region: 'for => return' in function 'func':
; PRAGMA-NEXT:  Calculated schedule:
; PRAGMA-NEXT:  domain: "[n] -> { Stmt_body[i0] : 0 <= i0 < n }"
; NOPRAGMA-LABEL: Printing analysis 'Polly - Optimize schedule of SCoP' for region: 'for => return' in function 'func':
; NOPRAGMA-NEXT:  Calculated schedule:
; NOPRAGMA-NEXT:  domain: "[n] -> { Stmt_body[i0] : 0 <= i0 < n }"
