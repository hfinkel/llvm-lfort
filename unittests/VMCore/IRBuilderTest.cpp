//===- llvm/unittest/VMCore/IRBuilderTest.cpp - IRBuilder tests -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/IRBuilder.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

class IRBuilderTest : public testing::Test {
protected:
  virtual void SetUp() {
    M.reset(new Module("MyModule", getGlobalContext()));
    FunctionType *FTy = FunctionType::get(Type::getVoidTy(getGlobalContext()),
                                          /*isVarArg=*/false);
    F = Function::Create(FTy, Function::ExternalLinkage, "", M.get());
    BB = BasicBlock::Create(getGlobalContext(), "", F);
    GV = new GlobalVariable(Type::getFloatTy(getGlobalContext()), true,
                            GlobalValue::ExternalLinkage);
  }

  virtual void TearDown() {
    BB = 0;
    M.reset();
  }

  OwningPtr<Module> M;
  Function *F;
  BasicBlock *BB;
  GlobalVariable *GV;
};

TEST_F(IRBuilderTest, Lifetime) {
  IRBuilder<> Builder(BB);
  AllocaInst *Var1 = Builder.CreateAlloca(Builder.getInt8Ty());
  AllocaInst *Var2 = Builder.CreateAlloca(Builder.getInt32Ty());
  AllocaInst *Var3 = Builder.CreateAlloca(Builder.getInt8Ty(),
                                          Builder.getInt32(123));

  CallInst *Start1 = Builder.CreateLifetimeStart(Var1);
  CallInst *Start2 = Builder.CreateLifetimeStart(Var2);
  CallInst *Start3 = Builder.CreateLifetimeStart(Var3, Builder.getInt64(100));

  EXPECT_EQ(Start1->getArgOperand(0), Builder.getInt64(-1));
  EXPECT_EQ(Start2->getArgOperand(0), Builder.getInt64(-1));
  EXPECT_EQ(Start3->getArgOperand(0), Builder.getInt64(100));

  EXPECT_EQ(Start1->getArgOperand(1), Var1);
  EXPECT_NE(Start2->getArgOperand(1), Var2);
  EXPECT_EQ(Start3->getArgOperand(1), Var3);

  Value *End1 = Builder.CreateLifetimeEnd(Var1);
  Builder.CreateLifetimeEnd(Var2);
  Builder.CreateLifetimeEnd(Var3);

  IntrinsicInst *II_Start1 = dyn_cast<IntrinsicInst>(Start1);
  IntrinsicInst *II_End1 = dyn_cast<IntrinsicInst>(End1);
  ASSERT_TRUE(II_Start1 != NULL);
  EXPECT_EQ(II_Start1->getIntrinsicID(), Intrinsic::lifetime_start);
  ASSERT_TRUE(II_End1 != NULL);
  EXPECT_EQ(II_End1->getIntrinsicID(), Intrinsic::lifetime_end);
}

TEST_F(IRBuilderTest, CreateCondBr) {
  IRBuilder<> Builder(BB);
  BasicBlock *TBB = BasicBlock::Create(getGlobalContext(), "", F);
  BasicBlock *FBB = BasicBlock::Create(getGlobalContext(), "", F);

  BranchInst *BI = Builder.CreateCondBr(Builder.getTrue(), TBB, FBB);
  TerminatorInst *TI = BB->getTerminator();
  EXPECT_EQ(BI, TI);
  EXPECT_EQ(2u, TI->getNumSuccessors());
  EXPECT_EQ(TBB, TI->getSuccessor(0));
  EXPECT_EQ(FBB, TI->getSuccessor(1));

  BI->eraseFromParent();
  MDNode *Weights = MDBuilder(getGlobalContext()).createBranchWeights(42, 13);
  BI = Builder.CreateCondBr(Builder.getTrue(), TBB, FBB, Weights);
  TI = BB->getTerminator();
  EXPECT_EQ(BI, TI);
  EXPECT_EQ(2u, TI->getNumSuccessors());
  EXPECT_EQ(TBB, TI->getSuccessor(0));
  EXPECT_EQ(FBB, TI->getSuccessor(1));
  EXPECT_EQ(Weights, TI->getMetadata(LLVMContext::MD_prof));
}

TEST_F(IRBuilderTest, LandingPadName) {
  IRBuilder<> Builder(BB);
  LandingPadInst *LP = Builder.CreateLandingPad(Builder.getInt32Ty(),
                                                Builder.getInt32(0), 0, "LP");
  EXPECT_EQ(LP->getName(), "LP");
}

TEST_F(IRBuilderTest, GetIntTy) {
  IRBuilder<> Builder(BB);
  IntegerType *Ty1 = Builder.getInt1Ty();
  EXPECT_EQ(Ty1, IntegerType::get(getGlobalContext(), 1));

  DataLayout* DL = new DataLayout(M.get());
  IntegerType *IntPtrTy = Builder.getIntPtrTy(DL);
  unsigned IntPtrBitSize =  DL->getPointerSizeInBits(0);
  EXPECT_EQ(IntPtrTy, IntegerType::get(getGlobalContext(), IntPtrBitSize));
}

TEST_F(IRBuilderTest, FastMathFlags) {
  IRBuilder<> Builder(BB);
  Value *F;
  Instruction *FDiv, *FAdd;

  F = Builder.CreateLoad(GV);
  F = Builder.CreateFAdd(F, F);

  EXPECT_FALSE(Builder.getFastMathFlags().any());
  ASSERT_TRUE(isa<Instruction>(F));
  FAdd = cast<Instruction>(F);
  EXPECT_FALSE(FAdd->hasNoNaNs());

  FastMathFlags FMF;
  Builder.SetFastMathFlags(FMF);

  F = Builder.CreateFAdd(F, F);
  EXPECT_FALSE(Builder.getFastMathFlags().any());

  FMF.setUnsafeAlgebra();
  Builder.SetFastMathFlags(FMF);

  F = Builder.CreateFAdd(F, F);
  EXPECT_TRUE(Builder.getFastMathFlags().any());
  ASSERT_TRUE(isa<Instruction>(F));
  FAdd = cast<Instruction>(F);
  EXPECT_TRUE(FAdd->hasNoNaNs());

  F = Builder.CreateFDiv(F, F);
  EXPECT_TRUE(Builder.getFastMathFlags().any());
  EXPECT_TRUE(Builder.getFastMathFlags().UnsafeAlgebra);
  ASSERT_TRUE(isa<Instruction>(F));
  FDiv = cast<Instruction>(F);
  EXPECT_TRUE(FDiv->hasAllowReciprocal());

  Builder.clearFastMathFlags();

  F = Builder.CreateFDiv(F, F);
  ASSERT_TRUE(isa<Instruction>(F));
  FDiv = cast<Instruction>(F);
  EXPECT_FALSE(FDiv->hasAllowReciprocal());

  FMF.clear();
  FMF.setAllowReciprocal();
  Builder.SetFastMathFlags(FMF);

  F = Builder.CreateFDiv(F, F);
  EXPECT_TRUE(Builder.getFastMathFlags().any());
  EXPECT_TRUE(Builder.getFastMathFlags().AllowReciprocal);
  ASSERT_TRUE(isa<Instruction>(F));
  FDiv = cast<Instruction>(F);
  EXPECT_TRUE(FDiv->hasAllowReciprocal());

  Builder.clearFastMathFlags();

  F = Builder.CreateFDiv(F, F);
  ASSERT_TRUE(isa<Instruction>(F));
  FDiv = cast<Instruction>(F);
  EXPECT_FALSE(FDiv->getFastMathFlags().any());
  FDiv->copyFastMathFlags(FAdd);
  EXPECT_TRUE(FDiv->hasNoNaNs());

}

}
