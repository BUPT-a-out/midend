#include <gtest/gtest.h>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Module.h"

using namespace midend;

namespace {

class InstructionTest : public ::testing::Test {
   protected:
    void SetUp() override {
        context = std::make_unique<Context>();
        module = std::make_unique<Module>("test", context.get());

        auto* voidTy = context->getVoidType();
        auto* fnTy = FunctionType::get(voidTy, {});
        function = Function::Create(fnTy, "test_func");
        module->push_back(function);

        bb = BasicBlock::Create(context.get(), "entry", function);

        builder = std::make_unique<IRBuilder>(bb);
    }

    std::unique_ptr<Context> context;
    std::unique_ptr<Module> module;
    Function* function;
    BasicBlock* bb;
    std::unique_ptr<IRBuilder> builder;
};

TEST_F(InstructionTest, BinaryOperations) {
    auto* lhs = builder->getInt32(10);
    auto* rhs = builder->getInt32(20);

    auto* add = builder->createAdd(lhs, rhs, "add_result");
    auto* addInst = static_cast<Instruction*>(add);
    EXPECT_TRUE(addInst->isBinaryOp());
    EXPECT_EQ(addInst->getOpcode(), Opcode::Add);
    EXPECT_EQ(addInst->getOperand(0), lhs);
    EXPECT_EQ(addInst->getOperand(1), rhs);
    EXPECT_EQ(addInst->getName(), "add_result");

    auto* sub = builder->createSub(lhs, rhs, "sub_result");
    auto* subInst = static_cast<Instruction*>(sub);
    EXPECT_EQ(subInst->getOpcode(), Opcode::Sub);

    auto* mul = builder->createMul(lhs, rhs, "mul_result");
    auto* mulInst = static_cast<Instruction*>(mul);
    EXPECT_EQ(mulInst->getOpcode(), Opcode::Mul);

    auto* div = builder->createDiv(lhs, rhs, "div_result");
    auto* divInst = static_cast<Instruction*>(div);
    EXPECT_EQ(divInst->getOpcode(), Opcode::Div);
}

TEST_F(InstructionTest, ComparisonOperations) {
    auto* lhs = builder->getInt32(10);
    auto* rhs = builder->getInt32(20);

    auto* eq = builder->createICmpEQ(lhs, rhs, "eq_result");
    auto* eqInst = static_cast<Instruction*>(eq);
    EXPECT_TRUE(eqInst->isComparison());
    EXPECT_EQ(eqInst->getOpcode(), Opcode::ICmpEQ);
    EXPECT_EQ(eq->getType(), context->getInt1Type());

    auto* ne = builder->createICmpNE(lhs, rhs, "ne_result");
    auto* neInst = static_cast<Instruction*>(ne);
    EXPECT_EQ(neInst->getOpcode(), Opcode::ICmpNE);

    auto* lt = builder->createICmpSLT(lhs, rhs, "lt_result");
    auto* ltInst = static_cast<Instruction*>(lt);
    EXPECT_EQ(ltInst->getOpcode(), Opcode::ICmpSLT);
}

TEST_F(InstructionTest, MemoryOperations) {
    auto* int32Ty = context->getInt32Type();

    auto* alloca = builder->createAlloca(int32Ty, nullptr, "local_var");
    EXPECT_TRUE(alloca->isMemoryOp());
    EXPECT_EQ(alloca->getOpcode(), Opcode::Alloca);
    EXPECT_EQ(alloca->getAllocatedType(), int32Ty);
    EXPECT_FALSE(alloca->isArrayAllocation());

    auto* value = builder->getInt32(42);
    auto* store = builder->createStore(value, alloca);
    EXPECT_EQ(store->getOpcode(), Opcode::Store);
    EXPECT_EQ(store->getValueOperand(), value);
    EXPECT_EQ(store->getPointerOperand(), alloca);

    auto* load = builder->createLoad(alloca, "loaded_value");
    EXPECT_EQ(load->getOpcode(), Opcode::Load);
    EXPECT_EQ(load->getPointerOperand(), alloca);
    EXPECT_EQ(load->getType(), int32Ty);
}

TEST_F(InstructionTest, ControlFlowInstructions) {
    // Return instruction
    auto* value = builder->getInt32(42);
    auto* ret = ReturnInst::Create(context.get(), value);
    EXPECT_TRUE(ret->isTerminator());
    EXPECT_EQ(ret->getOpcode(), Opcode::Ret);
    EXPECT_EQ(ret->getReturnValue(), value);

    // Branch instruction - unconditional
    auto* targetBB = BasicBlock::Create(context.get(), "target", function);

    auto* br = BranchInst::Create(targetBB);
    EXPECT_TRUE(br->isTerminator());
    EXPECT_TRUE(br->isUnconditional());
    EXPECT_FALSE(br->isConditional());
    EXPECT_EQ(br->getNumSuccessors(), 1u);
    EXPECT_EQ(br->getSuccessor(0), targetBB);

    // Branch instruction - conditional
    auto* cond = builder->getTrue();
    auto* trueBB = BasicBlock::Create(context.get(), "true_bb", function);
    auto* falseBB = BasicBlock::Create(context.get(), "false_bb", function);

    auto* condBr = BranchInst::Create(cond, trueBB, falseBB);
    EXPECT_TRUE(condBr->isConditional());
    EXPECT_FALSE(condBr->isUnconditional());
    EXPECT_EQ(condBr->getNumSuccessors(), 2u);
    EXPECT_EQ(condBr->getCondition(), cond);

    delete ret;
    delete br;
    delete condBr;
}

TEST_F(InstructionTest, PHINode) {
    auto* int32Ty = context->getInt32Type();

    auto* phi = builder->createPHI(int32Ty, "phi_node");
    EXPECT_EQ(phi->getOpcode(), Opcode::PHI);
    EXPECT_EQ(phi->getType(), int32Ty);
    EXPECT_EQ(phi->getNumIncomingValues(), 0u);

    auto* bb1 = BasicBlock::Create(context.get(), "bb1", function);
    auto* bb2 = BasicBlock::Create(context.get(), "bb2", function);

    auto* val1 = builder->getInt32(10);
    auto* val2 = builder->getInt32(20);

    phi->addIncoming(val1, bb1);
    phi->addIncoming(val2, bb2);

    EXPECT_EQ(phi->getNumIncomingValues(), 2u);
    EXPECT_EQ(phi->getIncomingValue(0), val1);
    EXPECT_EQ(phi->getIncomingBlock(0), bb1);
    EXPECT_EQ(phi->getIncomingValue(1), val2);
    EXPECT_EQ(phi->getIncomingBlock(1), bb2);

    EXPECT_EQ(phi->getBasicBlockIndex(bb1), 0);
    EXPECT_EQ(phi->getBasicBlockIndex(bb2), 1);
    EXPECT_EQ(phi->getIncomingValueForBlock(bb1), val1);
}

TEST_F(InstructionTest, CallInstruction) {
    // Create a function to call
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty});
    auto* callee = Function::Create(fnTy, "add_func");
    module->push_back(callee);

    std::vector<Value*> args = {builder->getInt32(10), builder->getInt32(20)};
    auto* call = builder->createCall(callee, args, "call_result");

    EXPECT_EQ(call->getOpcode(), Opcode::Call);
    EXPECT_EQ(call->getCalledFunction(), callee);
    EXPECT_EQ(call->getNumArgOperands(), 2u);
    EXPECT_EQ(call->getArgOperand(0), args[0]);
    EXPECT_EQ(call->getArgOperand(1), args[1]);
    EXPECT_EQ(call->getType(), int32Ty);
}

TEST_F(InstructionTest, SelectInstruction) {
    auto* cond = builder->getTrue();
    auto* trueVal = builder->getInt32(10);
    auto* falseVal = builder->getInt32(20);

    auto* select =
        builder->createSelect(cond, trueVal, falseVal, "select_result");

    EXPECT_EQ(select->getOpcode(), Opcode::Select);
    EXPECT_EQ(select->getCondition(), cond);
    EXPECT_EQ(select->getTrueValue(), trueVal);
    EXPECT_EQ(select->getFalseValue(), falseVal);
    EXPECT_EQ(select->getType(), trueVal->getType());
}

TEST_F(InstructionTest, InstructionMovement) {
    auto* inst1 =
        builder->createAlloca(context->getInt32Type(), nullptr, "var1");
    auto* inst2 =
        builder->createAlloca(context->getInt32Type(), nullptr, "var2");
    auto* inst3 =
        builder->createAlloca(context->getInt32Type(), nullptr, "var3");

    EXPECT_EQ(bb->size(), 3u);

    // Check initial order
    auto it = bb->begin();
    EXPECT_EQ(*it, inst1);
    ++it;
    EXPECT_EQ(*it, inst2);
    ++it;
    EXPECT_EQ(*it, inst3);

    // Move inst3 before inst1
    inst3->moveBefore(inst1);

    // Check new order
    it = bb->begin();
    EXPECT_EQ(*it, inst3);
    ++it;
    EXPECT_EQ(*it, inst1);
    ++it;
    EXPECT_EQ(*it, inst2);
}

}  // namespace