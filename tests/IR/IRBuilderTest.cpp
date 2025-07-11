#include <gtest/gtest.h>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Module.h"

using namespace midend;

namespace {

class IRBuilderTest : public ::testing::Test {
   protected:
    void SetUp() override {
        context = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", context.get());

        auto* voidTy = context->getVoidType();
        auto* fnTy = FunctionType::get(voidTy, {});
        function = Function::Create(fnTy, "test_function", module.get());
        entryBB = BasicBlock::Create(context.get(), "entry", function);

        builder = std::make_unique<IRBuilder>(entryBB);
    }

    std::unique_ptr<Context> context;
    std::unique_ptr<Module> module;
    Function* function;
    BasicBlock* entryBB;
    std::unique_ptr<IRBuilder> builder;
};

TEST_F(IRBuilderTest, BasicBlockOperations) {
    EXPECT_EQ(builder->getInsertBlock(), entryBB);

    auto* newBB = BasicBlock::Create(context.get(), "new_block", function);
    builder->setInsertPoint(newBB);
    EXPECT_EQ(builder->getInsertBlock(), newBB);

    auto* alloca = builder->createAlloca(context->getInt32Type());
    EXPECT_EQ(alloca->getParent(), newBB);
    EXPECT_EQ(&newBB->back(), alloca);
}

TEST_F(IRBuilderTest, UnaryOperations) {
    auto* int32Ty = context->getInt32Type();
    auto* val = ConstantInt::get(int32Ty, 42);

    auto* uaddInst = builder->createUAdd(val, "uadd_result");
    EXPECT_EQ(uaddInst->getOpcode(), Opcode::UAdd);
    EXPECT_EQ(uaddInst->getOperand(), val);
    EXPECT_EQ(uaddInst->getName(), "uadd_result");
    EXPECT_EQ(uaddInst->getType(), int32Ty);

    auto* usubInst = builder->createUSub(val, "usub_result");
    EXPECT_EQ(usubInst->getOpcode(), Opcode::USub);

    auto* boolVal = ConstantInt::getTrue(context.get());
    auto* notInst = builder->createNot(boolVal, "not_result");
    EXPECT_EQ(notInst->getOpcode(), Opcode::Not);
    EXPECT_TRUE(notInst->getType()->isIntegerType());
    EXPECT_EQ(notInst->getType()->getBitWidth(), 1u);
}

TEST_F(IRBuilderTest, BinaryOperations) {
    auto* int32Ty = context->getInt32Type();
    auto* val1 = ConstantInt::get(int32Ty, 10);
    auto* val2 = ConstantInt::get(int32Ty, 20);

    auto* addInst = builder->createAdd(val1, val2, "add_result");
    EXPECT_EQ(addInst->getOpcode(), Opcode::Add);
    EXPECT_EQ(addInst->getOperand(0), val1);
    EXPECT_EQ(addInst->getOperand(1), val2);
    EXPECT_EQ(addInst->getName(), "add_result");
    EXPECT_EQ(addInst->getType(), int32Ty);

    auto* subInst = builder->createSub(val1, val2, "sub_result");
    EXPECT_EQ(subInst->getOpcode(), Opcode::Sub);

    auto* mulInst = builder->createMul(val1, val2, "mul_result");
    EXPECT_EQ(mulInst->getOpcode(), Opcode::Mul);

    auto* divInst = builder->createDiv(val1, val2, "div_result");
    EXPECT_EQ(divInst->getOpcode(), Opcode::Div);

    auto* remInst = builder->createRem(val1, val2, "rem_result");
    EXPECT_EQ(remInst->getOpcode(), Opcode::Rem);
}

TEST_F(IRBuilderTest, LogicalOperations) {
    auto* boolVal1 = ConstantInt::getTrue(context.get());
    auto* boolVal2 = ConstantInt::getFalse(context.get());

    auto* landInst = builder->createLAnd(boolVal1, boolVal2, "land_result");
    EXPECT_EQ(landInst->getOpcode(), Opcode::LAnd);
    EXPECT_EQ(landInst->getOperand(0), boolVal1);
    EXPECT_EQ(landInst->getOperand(1), boolVal2);
    EXPECT_TRUE(landInst->getType()->isIntegerType());
    EXPECT_EQ(static_cast<IntegerType*>(landInst->getType())->getBitWidth(),
              1u);

    auto* lorInst = builder->createLOr(boolVal1, boolVal2, "lor_result");
    EXPECT_EQ(lorInst->getOpcode(), Opcode::LOr);
    EXPECT_EQ(lorInst->getOperand(0), boolVal1);
    EXPECT_EQ(lorInst->getOperand(1), boolVal2);
    EXPECT_TRUE(lorInst->getType()->isIntegerType());
    EXPECT_EQ(static_cast<IntegerType*>(lorInst->getType())->getBitWidth(), 1u);
}

TEST_F(IRBuilderTest, ComparisonOperations) {
    auto* int32Ty = context->getInt32Type();
    auto* val1 = ConstantInt::get(int32Ty, 10);
    auto* val2 = ConstantInt::get(int32Ty, 20);

    auto* cmpInst = builder->createICmpEQ(val1, val2, "eq_result");
    EXPECT_EQ(cmpInst->getOpcode(), Opcode::ICmpEQ);
    EXPECT_EQ(static_cast<CmpInst*>(cmpInst)->getPredicate(), CmpInst::ICMP_EQ);
    EXPECT_EQ(cmpInst->getOperand(0), val1);
    EXPECT_EQ(cmpInst->getOperand(1), val2);
    EXPECT_TRUE(cmpInst->getType()->isIntegerType());
    EXPECT_EQ(static_cast<IntegerType*>(cmpInst->getType())->getBitWidth(), 1u);

    auto* ltInst = builder->createICmpSLT(val1, val2, "lt_result");
    EXPECT_EQ(static_cast<CmpInst*>(ltInst)->getPredicate(), CmpInst::ICMP_SLT);

    auto* gtInst = builder->createICmpSGT(val1, val2, "gt_result");
    EXPECT_EQ(static_cast<CmpInst*>(gtInst)->getPredicate(), CmpInst::ICMP_SGT);
}

TEST_F(IRBuilderTest, MemoryOperations) {
    auto* int32Ty = context->getInt32Type();

    auto* allocaInst = builder->createAlloca(int32Ty, nullptr, "local_var");
    EXPECT_EQ(allocaInst->getOpcode(), Opcode::Alloca);
    EXPECT_EQ(allocaInst->getAllocatedType(), int32Ty);
    EXPECT_TRUE(allocaInst->getType()->isPointerType());

    auto* val = ConstantInt::get(int32Ty, 42);
    auto* storeInst = builder->createStore(val, allocaInst);
    EXPECT_EQ(storeInst->getOpcode(), Opcode::Store);
    EXPECT_EQ(storeInst->getValueOperand(), val);
    EXPECT_EQ(storeInst->getPointerOperand(), allocaInst);

    auto* loadInst = builder->createLoad(allocaInst, "loaded_val");
    EXPECT_EQ(loadInst->getOpcode(), Opcode::Load);
    EXPECT_EQ(loadInst->getPointerOperand(), allocaInst);
    EXPECT_EQ(loadInst->getType(), int32Ty);
    EXPECT_EQ(loadInst->getName(), "loaded_val");
}

TEST_F(IRBuilderTest, ControlFlowOperations) {
    auto* voidTy = context->getVoidType();
    auto* int32Ty = context->getInt32Type();

    // Test return instruction
    auto* retInst = builder->createRetVoid();
    EXPECT_EQ(retInst->getOpcode(), Opcode::Ret);
    EXPECT_EQ(retInst->getReturnValue(), nullptr);
    EXPECT_EQ(retInst->getType(), voidTy);

    // Create new block for more tests
    auto* newBB = BasicBlock::Create(context.get(), "new_block", function);
    builder->setInsertPoint(newBB);

    auto* val = ConstantInt::get(int32Ty, 42);
    auto* retValInst = builder->createRet(val);
    EXPECT_EQ(retValInst->getReturnValue(), val);
    EXPECT_EQ(retValInst->getType(), voidTy);

    // Test branch instruction
    builder->setInsertPoint(entryBB);
    auto* trueBB = BasicBlock::Create(context.get(), "true_block", function);
    auto* falseBB = BasicBlock::Create(context.get(), "false_block", function);

    auto* brInst = builder->createBr(trueBB);
    EXPECT_EQ(brInst->getOpcode(), Opcode::Br);
    EXPECT_TRUE(brInst->isUnconditional());
    EXPECT_EQ(brInst->getSuccessor(0), trueBB);
    EXPECT_EQ(brInst->getType(), voidTy);

    // Remove the unconditional branch and add a conditional one
    brInst->eraseFromParent();
    auto* cond = ConstantInt::getTrue(context.get());
    auto* condBrInst = builder->createCondBr(cond, trueBB, falseBB);
    EXPECT_TRUE(condBrInst->isConditional());
    EXPECT_EQ(condBrInst->getCondition(), cond);
    EXPECT_EQ(condBrInst->getSuccessor(0), trueBB);
    EXPECT_EQ(condBrInst->getSuccessor(1), falseBB);
    EXPECT_EQ(condBrInst->getType(), voidTy);
}

TEST_F(IRBuilderTest, CallOperations) {
    auto* int32Ty = context->getInt32Type();
    std::vector<Type*> params = {int32Ty, int32Ty};
    auto* fnTy = FunctionType::get(int32Ty, params);
    auto* callee = Function::Create(fnTy, "callee_function", module.get());

    auto* arg1 = ConstantInt::get(int32Ty, 10);
    auto* arg2 = ConstantInt::get(int32Ty, 20);
    std::vector<Value*> args = {arg1, arg2};

    auto* callInst = builder->createCall(callee, args, "call_result");
    EXPECT_EQ(callInst->getOpcode(), Opcode::Call);
    EXPECT_EQ(callInst->getCalledFunction(), callee);
    EXPECT_EQ(callInst->getNumArgOperands(), 2u);
    EXPECT_EQ(callInst->getArgOperand(0), arg1);
    EXPECT_EQ(callInst->getArgOperand(1), arg2);
    EXPECT_EQ(callInst->getType(), int32Ty);
    EXPECT_EQ(callInst->getName(), "call_result");
}

TEST_F(IRBuilderTest, CastOperations) {
    auto* int1Ty = context->getInt1Type();
    auto* int32Ty = context->getInt32Type();
    auto* floatTy = context->getFloatType();
    auto* val = ConstantInt::get(int32Ty, 42);

    // Test truncate from int32 to int1
    auto* truncInst = builder->createCast(CastInst::CastOps::Trunc, val, int1Ty,
                                          "trunc_result");
    EXPECT_EQ(static_cast<CastInst*>(truncInst)->getOpcode(), Opcode::Cast);
    EXPECT_EQ(static_cast<CastInst*>(truncInst)->getCastOpcode(),
              CastInst::Trunc);
    EXPECT_EQ(static_cast<Instruction*>(truncInst)->getOperand(0), val);
    EXPECT_EQ(truncInst->getType(), int1Ty);
    EXPECT_EQ(truncInst->getName(), "trunc_result");

    // Test sign extend from int1 to int32
    auto* sextInst = builder->createCast(CastInst::CastOps::SExt, truncInst,
                                         int32Ty, "sext_result");
    EXPECT_EQ(static_cast<CastInst*>(sextInst)->getCastOpcode(),
              CastInst::SExt);
    EXPECT_EQ(static_cast<Instruction*>(sextInst)->getOperand(0), truncInst);
    EXPECT_EQ(sextInst->getType(), int32Ty);

    // Test cast from int32 to float
    auto* siToFpInst = builder->createCast(CastInst::CastOps::SIToFP, val,
                                           floatTy, "sitofp_result");
    EXPECT_EQ(static_cast<CastInst*>(siToFpInst)->getCastOpcode(),
              CastInst::SIToFP);
    EXPECT_EQ(static_cast<Instruction*>(siToFpInst)->getOperand(0), val);
    EXPECT_EQ(siToFpInst->getType(), floatTy);
}

TEST_F(IRBuilderTest, PHIOperations) {
    auto* int32Ty = context->getInt32Type();

    auto* bb1 = BasicBlock::Create(context.get(), "bb1", function);
    auto* bb2 = BasicBlock::Create(context.get(), "bb2", function);
    auto* mergeBB = BasicBlock::Create(context.get(), "merge", function);

    builder->setInsertPoint(mergeBB);
    auto* phiInst = builder->createPHI(int32Ty, "phi_result");

    auto* val1 = ConstantInt::get(int32Ty, 10);
    auto* val2 = ConstantInt::get(int32Ty, 20);

    phiInst->addIncoming(val1, bb1);
    phiInst->addIncoming(val2, bb2);

    EXPECT_EQ(phiInst->getOpcode(), Opcode::PHI);
    EXPECT_EQ(phiInst->getType(), int32Ty);
    EXPECT_EQ(phiInst->getNumIncomingValues(), 2u);
    EXPECT_EQ(phiInst->getIncomingValue(0), val1);
    EXPECT_EQ(phiInst->getIncomingValue(1), val2);
    EXPECT_EQ(phiInst->getIncomingBlock(0), bb1);
    EXPECT_EQ(phiInst->getIncomingBlock(1), bb2);
}

}  // namespace