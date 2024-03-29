; RUN: opt < %s  -loop-vectorize -force-vector-unroll=1 -force-vector-width=4 -dce -instcombine -licm -S | FileCheck %s

target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx10.8.0"

@a = common global [2048 x i32] zeroinitializer, align 16
@b = common global [2048 x i32] zeroinitializer, align 16
@c = common global [2048 x i32] zeroinitializer, align 16

;CHECK: @example1
;CHECK-NOT: load <4 x i32>
;CHECK: ret void
define void @example1() nounwind uwtable ssp {
  br label %1

; <label>:1                                       ; preds = %1, %0
  %indvars.iv = phi i64 [ 0, %0 ], [ %indvars.iv.next, %1 ]
  %2 = getelementptr inbounds [2048 x i32]* @b, i64 0, i64 %indvars.iv
  %3 = load i32* %2, align 4
  %4 = getelementptr inbounds [2048 x i32]* @c, i64 0, i64 %indvars.iv
  %5 = load i32* %4, align 4
  %6 = add nsw i32 %5, %3
  %7 = getelementptr inbounds [2048 x i32]* @a, i64 0, i64 %indvars.iv
  store i32 %6, i32* %7, align 4
  %indvars.iv.next = add i64 %indvars.iv, 1
  %lftr.wideiv = trunc i64 %indvars.iv.next to i32
  %exitcond = icmp eq i32 %lftr.wideiv, 8  ;   <-----  A really small trip count.
  br i1 %exitcond, label %8, label %1

; <label>:8                                       ; preds = %1
  ret void
}

