; RUN: opt %loadNPMPolly -S \
; RUN:   -passes='polly-prepare,scop(polly-opt-isl,polly-codegen)' \
; RUN:   -polly-process-unprofitable \
; RUN:   -polly-allow-nonaffine \
; RUN:   -polly-force-offset-fusion=1 \
; RUN:   < %s | FileCheck %s
;
; Verify that a source-level friendly 3-way STL-like pipeline not only gets a
; fused schedule, but also keeps a live Polly runtime check in the generated
; LLVM IR instead of falling back to a constant false dispatch.

target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx26.0.0"

; CHECK-LABEL: define void @stl_like_offset_iota_transform_replace_copy(
; CHECK: entry:
; CHECK-NEXT:   br label %polly.split_new_and_old
; CHECK: polly.split_new_and_old:
; CHECK:   %polly.rtc.result = and i1
; CHECK-NEXT:   br i1 %polly.rtc.result, label %polly.start, label %entry.split.pre_entry_bb
; CHECK: polly.start:
; CHECK: polly.loop_header:
; CHECK: polly.stmt.for.body.i:
; CHECK: polly.stmt.for.body.i19:
; CHECK: polly.stmt.for.body.i22:
; CHECK: attributes #0 = { {{.*}}"polly-optimized"{{.*}} }
; CHECK-NOT: br i1 false, label %polly.start

define void @stl_like_offset_iota_transform_replace_copy(ptr noundef %tmp, ptr nocapture noundef writeonly %out, i64 noundef %n, i32 noundef %seed) local_unnamed_addr #0 {
entry:
  %cmp = icmp ult i64 %n, 2
  br i1 %cmp, label %return, label %if.end

if.end:                                           ; preds = %entry
  %add.ptr.idx = shl nsw i64 %n, 2
  %add.ptr = getelementptr inbounds i8, ptr %tmp, i64 %add.ptr.idx
  br label %for.body.i

for.body.i:                                       ; preds = %if.end, %for.body.i
  %__value.addr.06.i = phi i32 [ %inc.i, %for.body.i ], [ %seed, %if.end ]
  %__first.addr.05.i = phi ptr [ %incdec.ptr.i, %for.body.i ], [ %tmp, %if.end ]
  store i32 %__value.addr.06.i, ptr %__first.addr.05.i, align 4, !tbaa !5
  %incdec.ptr.i = getelementptr inbounds i8, ptr %__first.addr.05.i, i64 4
  %inc.i = add nsw i32 %__value.addr.06.i, 1
  %cmp.not.i = icmp eq ptr %incdec.ptr.i, %add.ptr
  br i1 %cmp.not.i, label %_ZNSt3__14iotaB8nn200100IPiiEEvT_S2_T0_.exit, label %for.body.i, !llvm.loop !9

_ZNSt3__14iotaB8nn200100IPiiEEvT_S2_T0_.exit:     ; preds = %for.body.i
  %add.ptr1 = getelementptr inbounds i8, ptr %tmp, i64 4
  br label %for.body.i19

for.body.i19:                                     ; preds = %_ZNSt3__14iotaB8nn200100IPiiEEvT_S2_T0_.exit, %for.body.i19
  %__result.addr.08.i = phi ptr [ %incdec.ptr1.i, %for.body.i19 ], [ %add.ptr1, %_ZNSt3__14iotaB8nn200100IPiiEEvT_S2_T0_.exit ]
  %0 = load i32, ptr %__result.addr.08.i, align 4, !tbaa !5
  %mul.i.i = shl nsw i32 %0, 1
  %add.i.i = or disjoint i32 %mul.i.i, 1
  store i32 %add.i.i, ptr %__result.addr.08.i, align 4, !tbaa !5
  %incdec.ptr1.i = getelementptr i8, ptr %__result.addr.08.i, i64 4
  %cmp.not.i21 = icmp eq ptr %incdec.ptr1.i, %add.ptr
  br i1 %cmp.not.i21, label %_ZNSt3__19transformB8nn200100IPiS1_Z43stl_like_offset_iota_transform_replace_copyE3$_0EET0_T_S4_S3_T1_.exit, label %for.body.i19, !llvm.loop !12

_ZNSt3__19transformB8nn200100IPiS1_Z43stl_like_offset_iota_transform_replace_copyE3$_0EET0_T_S4_S3_T1_.exit: ; preds = %for.body.i19
  %add.ptr5 = getelementptr inbounds i8, ptr %add.ptr, i64 -4
  %sub = add nsw i32 %seed, -1
  %add = add nsw i32 %seed, 7
  %cmp.not9.i = icmp eq ptr %add.ptr5, %tmp
  br i1 %cmp.not9.i, label %return, label %for.body.i22

for.body.i22:                                     ; preds = %_ZNSt3__19transformB8nn200100IPiS1_Z43stl_like_offset_iota_transform_replace_copyE3$_0EET0_T_S4_S3_T1_.exit, %for.body.i22
  %__first.addr.011.i = phi ptr [ %incdec.ptr.i23, %for.body.i22 ], [ %tmp, %_ZNSt3__19transformB8nn200100IPiS1_Z43stl_like_offset_iota_transform_replace_copyE3$_0EET0_T_S4_S3_T1_.exit ]
  %__result.addr.010.i = phi ptr [ %incdec.ptr2.i, %for.body.i22 ], [ %out, %_ZNSt3__19transformB8nn200100IPiS1_Z43stl_like_offset_iota_transform_replace_copyE3$_0EET0_T_S4_S3_T1_.exit ]
  %1 = load i32, ptr %__first.addr.011.i, align 4, !tbaa !5
  %cmp1.i = icmp eq i32 %1, %sub
  %storemerge.i = select i1 %cmp1.i, i32 %add, i32 %1
  store i32 %storemerge.i, ptr %__result.addr.010.i, align 4, !tbaa !5
  %incdec.ptr.i23 = getelementptr inbounds i8, ptr %__first.addr.011.i, i64 4
  %incdec.ptr2.i = getelementptr inbounds i8, ptr %__result.addr.010.i, i64 4
  %cmp.not.i24 = icmp eq ptr %incdec.ptr.i23, %add.ptr5
  br i1 %cmp.not.i24, label %return, label %for.body.i22, !llvm.loop !13

return:                                           ; preds = %for.body.i22, %_ZNSt3__19transformB8nn200100IPiS1_Z43stl_like_offset_iota_transform_replace_copyE3$_0EET0_T_S4_S3_T1_.exit, %entry
  ret void
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind ssp memory(readwrite, inaccessiblemem: none) "frame-pointer"="non-leaf" "no-trapping-math"="true" "probe-stack"="__chkstk_darwin" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+bti,+ccdp,+ccidx,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8.5a,+v8a,+zcm,+zcz" }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 26, i32 2]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"frame-pointer", i32 1}
!4 = !{!"Apple clang version 17.0.0 (clang-1700.6.4.2)"}
!5 = !{!6, !6, i64 0}
!6 = !{!"int", !7, i64 0}
!7 = !{!"omnipotent char", !8, i64 0}
!8 = !{!"Simple C++ TBAA"}
!9 = distinct !{!9, !10, !11}
!10 = !{!"llvm.loop.mustprogress"}
!11 = !{!"llvm.loop.unroll.disable"}
!12 = distinct !{!12, !10, !11}
!13 = distinct !{!13, !10, !11}
