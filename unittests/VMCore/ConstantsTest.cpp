//===- llvm/unittest/VMCore/ConstantsTest.cpp - Constants unit tests ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "gtest/gtest.h"

namespace llvm {
namespace {

TEST(ConstantsTest, Integer_i1) {
  IntegerType* Int1 = IntegerType::get(getGlobalContext(), 1);
  Constant* One = ConstantInt::get(Int1, 1, true);
  Constant* Zero = ConstantInt::get(Int1, 0);
  Constant* NegOne = ConstantInt::get(Int1, static_cast<uint64_t>(-1), true);
  EXPECT_EQ(NegOne, ConstantInt::getSigned(Int1, -1));
  Constant* Undef = UndefValue::get(Int1);

  // Input:  @b = constant i1 add(i1 1 , i1 1)
  // Output: @b = constant i1 false
  EXPECT_EQ(Zero, ConstantExpr::getAdd(One, One));

  // @c = constant i1 add(i1 -1, i1 1)
  // @c = constant i1 false
  EXPECT_EQ(Zero, ConstantExpr::getAdd(NegOne, One));

  // @d = constant i1 add(i1 -1, i1 -1)
  // @d = constant i1 false
  EXPECT_EQ(Zero, ConstantExpr::getAdd(NegOne, NegOne));

  // @e = constant i1 sub(i1 -1, i1 1)
  // @e = constant i1 false
  EXPECT_EQ(Zero, ConstantExpr::getSub(NegOne, One));

  // @f = constant i1 sub(i1 1 , i1 -1)
  // @f = constant i1 false
  EXPECT_EQ(Zero, ConstantExpr::getSub(One, NegOne));

  // @g = constant i1 sub(i1 1 , i1 1)
  // @g = constant i1 false
  EXPECT_EQ(Zero, ConstantExpr::getSub(One, One));

  // @h = constant i1 shl(i1 1 , i1 1)  ; undefined
  // @h = constant i1 undef
  EXPECT_EQ(Undef, ConstantExpr::getShl(One, One));

  // @i = constant i1 shl(i1 1 , i1 0)
  // @i = constant i1 true
  EXPECT_EQ(One, ConstantExpr::getShl(One, Zero));

  // @j = constant i1 lshr(i1 1, i1 1)  ; undefined
  // @j = constant i1 undef
  EXPECT_EQ(Undef, ConstantExpr::getLShr(One, One));

  // @m = constant i1 ashr(i1 1, i1 1)  ; undefined
  // @m = constant i1 undef
  EXPECT_EQ(Undef, ConstantExpr::getAShr(One, One));

  // @n = constant i1 mul(i1 -1, i1 1)
  // @n = constant i1 true
  EXPECT_EQ(One, ConstantExpr::getMul(NegOne, One));

  // @o = constant i1 sdiv(i1 -1, i1 1) ; overflow
  // @o = constant i1 true
  EXPECT_EQ(One, ConstantExpr::getSDiv(NegOne, One));

  // @p = constant i1 sdiv(i1 1 , i1 -1); overflow
  // @p = constant i1 true
  EXPECT_EQ(One, ConstantExpr::getSDiv(One, NegOne));

  // @q = constant i1 udiv(i1 -1, i1 1)
  // @q = constant i1 true
  EXPECT_EQ(One, ConstantExpr::getUDiv(NegOne, One));

  // @r = constant i1 udiv(i1 1, i1 -1)
  // @r = constant i1 true
  EXPECT_EQ(One, ConstantExpr::getUDiv(One, NegOne));

  // @s = constant i1 srem(i1 -1, i1 1) ; overflow
  // @s = constant i1 false
  EXPECT_EQ(Zero, ConstantExpr::getSRem(NegOne, One));

  // @t = constant i1 urem(i1 -1, i1 1)
  // @t = constant i1 false
  EXPECT_EQ(Zero, ConstantExpr::getURem(NegOne, One));

  // @u = constant i1 srem(i1  1, i1 -1) ; overflow
  // @u = constant i1 false
  EXPECT_EQ(Zero, ConstantExpr::getSRem(One, NegOne));
}

TEST(ConstantsTest, IntSigns) {
  IntegerType* Int8Ty = Type::getInt8Ty(getGlobalContext());
  EXPECT_EQ(100, ConstantInt::get(Int8Ty, 100, false)->getSExtValue());
  EXPECT_EQ(100, ConstantInt::get(Int8Ty, 100, true)->getSExtValue());
  EXPECT_EQ(100, ConstantInt::getSigned(Int8Ty, 100)->getSExtValue());
  EXPECT_EQ(-50, ConstantInt::get(Int8Ty, 206)->getSExtValue());
  EXPECT_EQ(-50, ConstantInt::getSigned(Int8Ty, -50)->getSExtValue());
  EXPECT_EQ(206U, ConstantInt::getSigned(Int8Ty, -50)->getZExtValue());

  // Overflow is handled by truncation.
  EXPECT_EQ(0x3b, ConstantInt::get(Int8Ty, 0x13b)->getSExtValue());
}

TEST(ConstantsTest, FP128Test) {
  Type *FP128Ty = Type::getFP128Ty(getGlobalContext());

  IntegerType *Int128Ty = Type::getIntNTy(getGlobalContext(), 128);
  Constant *Zero128 = Constant::getNullValue(Int128Ty);
  Constant *X = ConstantExpr::getUIToFP(Zero128, FP128Ty);
  EXPECT_TRUE(isa<ConstantFP>(X));
}

#define CHECK(x, y) {                                           \
    std::string __s;                                            \
    raw_string_ostream __o(__s);                                \
    cast<ConstantExpr>(x)->getAsInstruction()->print(__o);      \
    __o.flush();                                                \
    EXPECT_EQ(std::string("  <badref> = " y), __s);             \
  }

TEST(ConstantsTest, AsInstructionsTest) {
  Module *M = new Module("MyModule", getGlobalContext());

  Type *Int64Ty = Type::getInt64Ty(getGlobalContext());
  Type *Int32Ty = Type::getInt32Ty(getGlobalContext());
  Type *Int16Ty = Type::getInt16Ty(getGlobalContext());
  Type *Int1Ty = Type::getInt1Ty(getGlobalContext());
  Type *FloatTy = Type::getFloatTy(getGlobalContext());
  Type *DoubleTy = Type::getDoubleTy(getGlobalContext());

  Constant *Global = M->getOrInsertGlobal("dummy",
                                         PointerType::getUnqual(Int32Ty));
  Constant *Global2 = M->getOrInsertGlobal("dummy2",
                                         PointerType::getUnqual(Int32Ty));

  Constant *P0 = ConstantExpr::getPtrToInt(Global, Int32Ty);
  Constant *P1 = ConstantExpr::getUIToFP(P0, FloatTy);
  Constant *P2 = ConstantExpr::getUIToFP(P0, DoubleTy);
  Constant *P3 = ConstantExpr::getTrunc(P0, Int1Ty);
  Constant *P4 = ConstantExpr::getPtrToInt(Global2, Int32Ty);
  Constant *P5 = ConstantExpr::getUIToFP(P4, FloatTy);
  Constant *P6 = ConstantExpr::getBitCast(P4, VectorType::get(Int16Ty, 2));

  Constant *One = ConstantInt::get(Int32Ty, 1);

  #define P0STR "ptrtoint (i32** @dummy to i32)"
  #define P1STR "uitofp (i32 ptrtoint (i32** @dummy to i32) to float)"
  #define P2STR "uitofp (i32 ptrtoint (i32** @dummy to i32) to double)"
  #define P3STR "ptrtoint (i32** @dummy to i1)"
  #define P4STR "ptrtoint (i32** @dummy2 to i32)"
  #define P5STR "uitofp (i32 ptrtoint (i32** @dummy2 to i32) to float)"
  #define P6STR "bitcast (i32 ptrtoint (i32** @dummy2 to i32) to <2 x i16>)"

  CHECK(ConstantExpr::getNeg(P0), "sub i32 0, " P0STR);
  CHECK(ConstantExpr::getFNeg(P1), "fsub float -0.000000e+00, " P1STR);
  CHECK(ConstantExpr::getNot(P0), "xor i32 " P0STR ", -1");
  CHECK(ConstantExpr::getAdd(P0, P0), "add i32 " P0STR ", " P0STR);
  CHECK(ConstantExpr::getAdd(P0, P0, false, true), "add nsw i32 " P0STR ", "
        P0STR);
  CHECK(ConstantExpr::getAdd(P0, P0, true, true), "add nuw nsw i32 " P0STR ", "
        P0STR);
  CHECK(ConstantExpr::getFAdd(P1, P1), "fadd float " P1STR ", " P1STR);
  CHECK(ConstantExpr::getSub(P0, P0), "sub i32 " P0STR ", " P0STR);
  CHECK(ConstantExpr::getFSub(P1, P1), "fsub float " P1STR ", " P1STR);
  CHECK(ConstantExpr::getMul(P0, P0), "mul i32 " P0STR ", " P0STR);
  CHECK(ConstantExpr::getFMul(P1, P1), "fmul float " P1STR ", " P1STR);
  CHECK(ConstantExpr::getUDiv(P0, P0), "udiv i32 " P0STR ", " P0STR);
  CHECK(ConstantExpr::getSDiv(P0, P0), "sdiv i32 " P0STR ", " P0STR);
  CHECK(ConstantExpr::getFDiv(P1, P1), "fdiv float " P1STR ", " P1STR);
  CHECK(ConstantExpr::getURem(P0, P0), "urem i32 " P0STR ", " P0STR);
  CHECK(ConstantExpr::getSRem(P0, P0), "srem i32 " P0STR ", " P0STR);
  CHECK(ConstantExpr::getFRem(P1, P1), "frem float " P1STR ", " P1STR);
  CHECK(ConstantExpr::getAnd(P0, P0), "and i32 " P0STR ", " P0STR);
  CHECK(ConstantExpr::getOr(P0, P0), "or i32 " P0STR ", " P0STR);
  CHECK(ConstantExpr::getXor(P0, P0), "xor i32 " P0STR ", " P0STR);
  CHECK(ConstantExpr::getShl(P0, P0), "shl i32 " P0STR ", " P0STR);
  CHECK(ConstantExpr::getShl(P0, P0, true), "shl nuw i32 " P0STR ", " P0STR);
  CHECK(ConstantExpr::getShl(P0, P0, false, true), "shl nsw i32 " P0STR ", "
        P0STR);
  CHECK(ConstantExpr::getLShr(P0, P0, false), "lshr i32 " P0STR ", " P0STR);
  CHECK(ConstantExpr::getLShr(P0, P0, true), "lshr exact i32 " P0STR ", " P0STR);
  CHECK(ConstantExpr::getAShr(P0, P0, false), "ashr i32 " P0STR ", " P0STR);
  CHECK(ConstantExpr::getAShr(P0, P0, true), "ashr exact i32 " P0STR ", " P0STR);

  CHECK(ConstantExpr::getSExt(P0, Int64Ty), "sext i32 " P0STR " to i64");
  CHECK(ConstantExpr::getZExt(P0, Int64Ty), "zext i32 " P0STR " to i64");
  CHECK(ConstantExpr::getFPTrunc(P2, FloatTy), "fptrunc double " P2STR
        " to float");
  CHECK(ConstantExpr::getFPExtend(P1, DoubleTy), "fpext float " P1STR
        " to double");

  CHECK(ConstantExpr::getExactUDiv(P0, P0), "udiv exact i32 " P0STR ", " P0STR);

  CHECK(ConstantExpr::getSelect(P3, P0, P4), "select i1 " P3STR ", i32 " P0STR
        ", i32 " P4STR);
  CHECK(ConstantExpr::getICmp(CmpInst::ICMP_EQ, P0, P4), "icmp eq i32 " P0STR
        ", " P4STR);
  CHECK(ConstantExpr::getFCmp(CmpInst::FCMP_ULT, P1, P5), "fcmp ult float "
        P1STR ", " P5STR);

  std::vector<Constant*> V;
  V.push_back(One);
  // FIXME: getGetElementPtr() actually creates an inbounds ConstantGEP,
  //        not a normal one!
  //CHECK(ConstantExpr::getGetElementPtr(Global, V, false),
  //      "getelementptr i32** @dummy, i32 1");
  CHECK(ConstantExpr::getInBoundsGetElementPtr(Global, V),
        "getelementptr inbounds i32** @dummy, i32 1");

  CHECK(ConstantExpr::getExtractElement(P6, One), "extractelement <2 x i16> "
        P6STR ", i32 1");
}

#undef CHECK

}  // end anonymous namespace
}  // end namespace llvm
