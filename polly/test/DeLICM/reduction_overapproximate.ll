; RUN: opt %loadPolly -polly-stmt-granularity=bb -polly-flatten-schedule -polly-delicm-compute-known=true -polly-delicm-overapproximate-writes=true -polly-delicm-partial-writes=false -polly-print-delicm -pass-remarks-analysis="polly-scops" -disable-output < %s 2>&1 | FileCheck %s --check-prefix=DISMISSED
; RUN: opt %loadPolly -polly-stmt-granularity=bb -polly-flatten-schedule -polly-delicm-compute-known=true -polly-delicm-overapproximate-writes=false -polly-delicm-partial-writes=false  -polly-print-delicm -pass-remarks-analysis="polly-scops" -disable-output < %s 2>&1 | FileCheck %s --check-prefix=DISMISSED
; RUN: opt %loadPolly -polly-stmt-granularity=bb -polly-flatten-schedule -polly-delicm-compute-known=true -polly-delicm-partial-writes=true -polly-print-delicm -pass-remarks-analysis="polly-scops" -disable-output < %s 2>&1 | FileCheck %s --check-prefix=DISMISSED
;
;    void func(double *A {
;      for (int j = -1; j < 3; j += 1) { /* outer */
;        double phi = 0.0;
;        if (0 < j)
;          for (int i = 0; i < j; i += 1) /* reduction */
;            phi += 4.2;
;        A[j] = phi;
;      }
;    }
;
; The current builder conservatively dismisses this corner case before DeLICM
; can rewrite it. Keep the regression explicit: no optimized Polly path is
; generated, which is a missed optimization rather than a miscompile risk.
; DISMISSED:      remark: <unknown>:0:0: SCoP begins here.
; DISMISSED-NEXT: remark: <unknown>:0:0: SCoP ends here but was dismissed.
;
define void @func(ptr noalias nonnull %A) {
entry:
  br label %outer.preheader

outer.preheader:
  br label %outer.for

outer.for:
  %j = phi i32 [-1, %outer.preheader], [%j.inc, %outer.inc]
  %j.cmp = icmp slt i32 %j, 3
  br i1 %j.cmp, label %reduction.checkloop, label %outer.exit



    reduction.checkloop:
      %j2.cmp = icmp slt i32 0, %j
      br i1 %j2.cmp, label %reduction.preheader, label %reduction.exit

    reduction.preheader:
      br label %reduction.for

    reduction.for:
      %i = phi i32 [0, %reduction.preheader], [%i.inc, %reduction.inc]
      %phi = phi double [0.0, %reduction.preheader], [%add, %reduction.inc]
      br label %body



        body:
          %add = fadd double %phi, 4.2
          br label %reduction.inc



    reduction.inc:
      %i.inc = add nuw nsw i32 %i, 1
      %i.cmp = icmp slt i32 %i.inc, %j
      br i1 %i.cmp, label %reduction.for, label %reduction.exit

    reduction.exit:
      %val = phi double [%add, %reduction.inc], [0.0, %reduction.checkloop]
      %A_idx = getelementptr inbounds double, ptr %A, i32 %j
      store double %val, ptr %A_idx
      br label %outer.inc



outer.inc:
  %j.inc = add nuw nsw i32 %j, 1
  br label %outer.for

outer.exit:
  br label %return

return:
  ret void
}
