; RUN: opt %loadNPMPolly -polly-reschedule=0 -polly-loopfusion-greedy=1 -polly-postopts=0 '-passes=print<polly-opt-isl>' -disable-output < %s | FileCheck %s --check-prefix=RES0
; RUN: opt %loadNPMPolly -polly-reschedule=1 -polly-loopfusion-greedy=1 -polly-postopts=0 '-passes=print<polly-opt-isl>' -disable-output < %s | FileCheck %s --check-prefix=RES1

define void @func(i32 %n, ptr noalias nonnull %A, ptr noalias nonnull %B, i32 %k) {
entry:
  br label %for1


for1:
  %j1 = phi i32 [0, %entry], [%j1.inc, %inc1]
  %j1.cmp = icmp slt i32 %j1, %n
  br i1 %j1.cmp, label %body1, label %exit1

    body1:
      %idx1 = add i32 %j1, %k
      %arrayidx1 = getelementptr inbounds double, ptr %A, i32 %j1
      store double 21.0, ptr %arrayidx1
      br label %inc1

inc1:
  %j1.inc = add nuw nsw i32 %j1, 1
  br label %for1

exit1:
  br label %middle2


middle2:
  store double 52.0, ptr %A
  br label %for3


for3:
  %j3 = phi i32 [0, %middle2], [%j3.inc, %inc3]
  %j3.cmp = icmp slt i32 %j3, %n
  br i1 %j3.cmp, label %body3, label %exit3

    body3:
      %arrayidx3 = getelementptr inbounds double, ptr %B, i32 %j3
      store double 84.0, ptr %arrayidx3
      br label %inc3

inc3:
  %j3.inc = add nuw nsw i32 %j3, 1
  br label %for3

exit3:
  br label %return


return:
  ret void
}


; RES0:      Calculated schedule:
; RES0-NEXT: n/a
; RES1:      Calculated schedule:
; RES1-NEXT: domain: "[n] -> { Stmt_body3[i0] : 0 <= i0 < n; Stmt_middle2[]; Stmt_body1[i0] : 0 <= i0 < n }"
; RES1-NEXT: child:
; RES1-NEXT:   sequence:
; RES1-NEXT:   - filter: "[n] -> { Stmt_body1[i0] }"
; RES1-NEXT:     child:
; RES1-NEXT:       schedule: "[n] -> [{ Stmt_body1[i0] -> [(i0)] }]"
; RES1-NEXT:       permutable: 1
; RES1-NEXT:       coincident: [ 1 ]
; RES1-NEXT:   - filter: "[n] -> { Stmt_middle2[] }"
; RES1-NEXT:   - filter: "[n] -> { Stmt_body3[i0] }"
; RES1-NEXT:     child:
; RES1-NEXT:       schedule: "[n] -> [{ Stmt_body3[i0] -> [(i0)] }]"
