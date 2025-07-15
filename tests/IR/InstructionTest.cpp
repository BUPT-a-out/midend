#include <gtest/gtest.h>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Instructions/BinaryOps.h"
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

TEST_F(InstructionTest, MemoryOpsDirectCreateWithParent) {
    auto* int32Ty = context->getInt32Type();
    auto* arraySize = builder->getInt32(10);
    auto* value = builder->getInt32(42);

    // Test AllocaInst::Create with parent parameter
    auto* alloca = AllocaInst::Create(int32Ty, nullptr, "alloca_test", bb);
    EXPECT_EQ(alloca->getParent(), bb);
    EXPECT_EQ(alloca->getOpcode(), Opcode::Alloca);
    EXPECT_EQ(alloca->getAllocatedType(), int32Ty);
    EXPECT_FALSE(alloca->isArrayAllocation());

    // Test AllocaInst::Create with array size and parent parameter
    auto* arrayAlloca =
        AllocaInst::Create(int32Ty, arraySize, "array_alloca", bb);
    EXPECT_EQ(arrayAlloca->getParent(), bb);
    EXPECT_EQ(arrayAlloca->getOpcode(), Opcode::Alloca);
    EXPECT_EQ(arrayAlloca->getAllocatedType(), int32Ty);
    EXPECT_TRUE(arrayAlloca->isArrayAllocation());
    EXPECT_EQ(arrayAlloca->getArraySize(), arraySize);

    // Test LoadInst::Create with parent parameter
    auto* load = LoadInst::Create(alloca, "load_test", bb);
    EXPECT_EQ(load->getParent(), bb);
    EXPECT_EQ(load->getOpcode(), Opcode::Load);
    EXPECT_EQ(load->getPointerOperand(), alloca);
    EXPECT_EQ(load->getType(), int32Ty);

    // Test StoreInst::Create with parent parameter
    auto* store = StoreInst::Create(value, alloca, bb);
    EXPECT_EQ(store->getParent(), bb);
    EXPECT_EQ(store->getOpcode(), Opcode::Store);
    EXPECT_EQ(store->getValueOperand(), value);
    EXPECT_EQ(store->getPointerOperand(), alloca);

    // Test GetElementPtrInst::Create with parent parameter
    std::vector<Value*> indices = {builder->getInt32(0), builder->getInt32(5)};
    auto* gep =
        GetElementPtrInst::Create(int32Ty, alloca, indices, "gep_test", bb);
    EXPECT_EQ(gep->getParent(), bb);
    EXPECT_EQ(gep->getOpcode(), Opcode::GetElementPtr);
    EXPECT_EQ(gep->getPointerOperand(), alloca);
    EXPECT_EQ(gep->getSourceElementType(), int32Ty);
    EXPECT_EQ(gep->getNumIndices(), 2u);
    EXPECT_EQ(gep->getIndex(0), indices[0]);
    EXPECT_EQ(gep->getIndex(1), indices[1]);

    // Verify all instructions are in the basic block
    EXPECT_EQ(bb->size(), 5u);
}

TEST_F(InstructionTest, GetElementPtrInstClone) {
    auto* int32Ty = context->getInt32Type();
    auto* alloca = builder->createAlloca(int32Ty, nullptr, "base_ptr");

    std::vector<Value*> indices = {builder->getInt32(0), builder->getInt32(5)};
    auto* gep =
        GetElementPtrInst::Create(int32Ty, alloca, indices, "gep_original");

    // Test clone functionality
    auto* cloned = gep->clone();
    auto* clonedGep = dyn_cast<GetElementPtrInst>(cloned);

    ASSERT_NE(clonedGep, nullptr);
    EXPECT_EQ(clonedGep->getPointerOperand(), alloca);
    EXPECT_EQ(clonedGep->getSourceElementType(), int32Ty);
    EXPECT_EQ(clonedGep->getNumIndices(), 2u);
    EXPECT_EQ(clonedGep->getIndex(0), indices[0]);
    EXPECT_EQ(clonedGep->getIndex(1), indices[1]);
    EXPECT_EQ(clonedGep->getName(), "gep_original");
    EXPECT_NE(clonedGep, gep);  // Different objects

    delete gep;
    delete cloned;
}

TEST_F(InstructionTest, OtherOpsDirectCreateWithParent) {
    auto* int32Ty = context->getInt32Type();
    auto* floatTy = context->getFloatType();

    // Create function for call test
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty});
    auto* callee = Function::Create(fnTy, "test_func");
    module->push_back(callee);

    std::vector<Value*> args = {builder->getInt32(10), builder->getInt32(20)};

    // Test CallInst::Create with FunctionType and parent parameter
    auto* call1 = CallInst::Create(fnTy, callee, args, "call1", bb);
    EXPECT_EQ(call1->getParent(), bb);
    EXPECT_EQ(call1->getOpcode(), Opcode::Call);
    EXPECT_EQ(call1->getCalledValue(), callee);
    EXPECT_EQ(call1->getNumArgOperands(), 2u);
    EXPECT_EQ(call1->getArgOperand(0), args[0]);
    EXPECT_EQ(call1->getArgOperand(1), args[1]);

    // Test CallInst::Create with Function and parent parameter
    auto* call2 = CallInst::Create(callee, args, "call2", bb);
    EXPECT_EQ(call2->getParent(), bb);
    EXPECT_EQ(call2->getOpcode(), Opcode::Call);
    EXPECT_EQ(call2->getCalledFunction(), callee);
    EXPECT_EQ(call2->getNumArgOperands(), 2u);

    // Test SelectInst::Create with parent parameter
    auto* cond = builder->getTrue();
    auto* trueVal = builder->getInt32(100);
    auto* falseVal = builder->getInt32(200);
    auto* select =
        SelectInst::Create(cond, trueVal, falseVal, "select_test", bb);
    EXPECT_EQ(select->getParent(), bb);
    EXPECT_EQ(select->getOpcode(), Opcode::Select);
    EXPECT_EQ(select->getCondition(), cond);
    EXPECT_EQ(select->getTrueValue(), trueVal);
    EXPECT_EQ(select->getFalseValue(), falseVal);

    // Test CastInst::Create with parent parameter
    auto* intVal = builder->getInt32(42);
    auto* castToFloat = CastInst::Create(CastInst::SIToFP, intVal, floatTy,
                                         "cast_to_float", bb);
    EXPECT_EQ(castToFloat->getParent(), bb);
    EXPECT_EQ(castToFloat->getOpcode(), Opcode::Cast);
    EXPECT_EQ(castToFloat->getCastOpcode(), CastInst::SIToFP);
    EXPECT_EQ(castToFloat->getOperand(0), intVal);
    EXPECT_EQ(castToFloat->getDestType(), floatTy);

    // Test MoveInst::Create with parent parameter
    auto* move = MoveInst::Create(intVal, "move_test", bb);
    EXPECT_EQ(move->getParent(), bb);
    EXPECT_EQ(move->getOpcode(), Opcode::Move);
    EXPECT_EQ(move->getValue(), intVal);

    // Verify all instructions are in the basic block
    EXPECT_EQ(bb->size(), 5u);
}

TEST_F(InstructionTest, CallInstClone) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty});
    auto* callee = Function::Create(fnTy, "test_func");
    module->push_back(callee);

    std::vector<Value*> args = {builder->getInt32(10), builder->getInt32(20)};
    auto* call = CallInst::Create(callee, args, "original_call");

    // Test clone functionality
    auto* cloned = call->clone();
    auto* clonedCall = dyn_cast<CallInst>(cloned);

    ASSERT_NE(clonedCall, nullptr);
    EXPECT_EQ(clonedCall->getCalledValue(), callee);
    EXPECT_EQ(clonedCall->getNumArgOperands(), 2u);
    EXPECT_EQ(clonedCall->getArgOperand(0), args[0]);
    EXPECT_EQ(clonedCall->getArgOperand(1), args[1]);
    EXPECT_EQ(clonedCall->getName(), "original_call");
    EXPECT_NE(clonedCall, call);  // Different objects

    delete call;
    delete cloned;
}

TEST_F(InstructionTest, TerminatorOpsDirectCreateWithParent) {
    auto* int32Ty = context->getInt32Type();
    auto* value = builder->getInt32(42);

    // Create additional basic blocks for testing
    auto* targetBB = BasicBlock::Create(context.get(), "target", function);
    auto* trueBB = BasicBlock::Create(context.get(), "true_bb", function);
    auto* falseBB = BasicBlock::Create(context.get(), "false_bb", function);

    // Test ReturnInst::Create with parent parameter
    auto* retBB = BasicBlock::Create(context.get(), "ret_bb", function);
    auto* ret = ReturnInst::Create(context.get(), value, retBB);
    EXPECT_EQ(ret->getParent(), retBB);
    EXPECT_EQ(ret->getOpcode(), Opcode::Ret);
    EXPECT_EQ(ret->getReturnValue(), value);

    // Test BranchInst::Create unconditional with parent parameter
    auto* brBB = BasicBlock::Create(context.get(), "br_bb", function);
    auto* unconditionalBr = BranchInst::Create(targetBB, brBB);
    EXPECT_EQ(unconditionalBr->getParent(), brBB);
    EXPECT_EQ(unconditionalBr->getOpcode(), Opcode::Br);
    EXPECT_TRUE(unconditionalBr->isUnconditional());
    EXPECT_EQ(unconditionalBr->getSuccessor(0), targetBB);

    // Test conditional branch
    auto* condBrBB = BasicBlock::Create(context.get(), "cond_br_bb", function);
    auto* cond = builder->getTrue();
    auto* conditionalBr = BranchInst::Create(cond, trueBB, falseBB, condBrBB);
    EXPECT_EQ(conditionalBr->getParent(), condBrBB);
    EXPECT_EQ(conditionalBr->getOpcode(), Opcode::Br);
    EXPECT_TRUE(conditionalBr->isConditional());
    EXPECT_EQ(conditionalBr->getCondition(), cond);
    EXPECT_EQ(conditionalBr->getSuccessor(0), trueBB);
    EXPECT_EQ(conditionalBr->getSuccessor(1), falseBB);

    // Test PHINode
    auto* phiBB = BasicBlock::Create(context.get(), "phi_bb", function);
    auto* phi = PHINode::Create(int32Ty, "phi_test", phiBB);
    EXPECT_EQ(phi->getParent(), phiBB);
    EXPECT_EQ(phi->getOpcode(), Opcode::PHI);
    EXPECT_EQ(phi->getType(), int32Ty);
    EXPECT_EQ(phi->getNumIncomingValues(), 0u);

    // Add some incoming values to the PHI
    auto* val1 = builder->getInt32(10);
    auto* val2 = builder->getInt32(20);
    phi->addIncoming(val1, trueBB);
    phi->addIncoming(val2, falseBB);

    EXPECT_EQ(phi->getNumIncomingValues(), 2u);
    EXPECT_EQ(phi->getIncomingValue(0), val1);
    EXPECT_EQ(phi->getIncomingBlock(0), trueBB);
    EXPECT_EQ(phi->getIncomingValue(1), val2);
    EXPECT_EQ(phi->getIncomingBlock(1), falseBB);

    // Verify all instructions are in their respective basic blocks
    EXPECT_EQ(retBB->size(), 1u);
    EXPECT_EQ(brBB->size(), 1u);
    EXPECT_EQ(condBrBB->size(), 1u);
    EXPECT_EQ(phiBB->size(), 1u);
}

TEST_F(InstructionTest, BranchInstClone) {
    auto* targetBB = BasicBlock::Create(context.get(), "target", function);
    auto* trueBB = BasicBlock::Create(context.get(), "true_bb", function);
    auto* falseBB = BasicBlock::Create(context.get(), "false_bb", function);

    // Test unconditional branch clone
    auto* unconditionalBr = BranchInst::Create(targetBB);
    auto* clonedUnconditional = unconditionalBr->clone();
    auto* clonedUnconditionalBr = dyn_cast<BranchInst>(clonedUnconditional);

    ASSERT_NE(clonedUnconditionalBr, nullptr);
    EXPECT_TRUE(clonedUnconditionalBr->isUnconditional());
    EXPECT_EQ(clonedUnconditionalBr->getSuccessor(0), targetBB);
    EXPECT_NE(clonedUnconditionalBr, unconditionalBr);  // Different objects

    // Test conditional branch clone
    auto* cond = builder->getTrue();
    auto* conditionalBr = BranchInst::Create(cond, trueBB, falseBB);
    auto* clonedConditional = conditionalBr->clone();
    auto* clonedConditionalBr = dyn_cast<BranchInst>(clonedConditional);

    ASSERT_NE(clonedConditionalBr, nullptr);
    EXPECT_TRUE(clonedConditionalBr->isConditional());
    EXPECT_EQ(clonedConditionalBr->getCondition(), cond);
    EXPECT_EQ(clonedConditionalBr->getSuccessor(0), trueBB);
    EXPECT_EQ(clonedConditionalBr->getSuccessor(1), falseBB);
    EXPECT_NE(clonedConditionalBr, conditionalBr);  // Different objects

    delete unconditionalBr;
    delete clonedUnconditional;
    delete conditionalBr;
    delete clonedConditional;
}

TEST_F(InstructionTest, PHINodeClone) {
    auto* int32Ty = context->getInt32Type();
    auto* trueBB = BasicBlock::Create(context.get(), "true_bb", function);
    auto* falseBB = BasicBlock::Create(context.get(), "false_bb", function);

    auto* phi = PHINode::Create(int32Ty, "original_phi");
    auto* val1 = builder->getInt32(10);
    auto* val2 = builder->getInt32(20);

    phi->addIncoming(val1, trueBB);
    phi->addIncoming(val2, falseBB);

    // Test clone functionality
    auto* cloned = phi->clone();
    auto* clonedPhi = dyn_cast<PHINode>(cloned);

    ASSERT_NE(clonedPhi, nullptr);
    EXPECT_EQ(clonedPhi->getType(), int32Ty);
    EXPECT_EQ(clonedPhi->getNumIncomingValues(), 2u);
    EXPECT_EQ(clonedPhi->getIncomingValue(0), val1);
    EXPECT_EQ(clonedPhi->getIncomingBlock(0), trueBB);
    EXPECT_EQ(clonedPhi->getIncomingValue(1), val2);
    EXPECT_EQ(clonedPhi->getIncomingBlock(1), falseBB);
    EXPECT_EQ(clonedPhi->getName(), "original_phi");
    EXPECT_NE(clonedPhi, phi);  // Different objects

    delete phi;
    delete cloned;
}

TEST_F(InstructionTest, InstructionModuleAndFunctionAccess) {
    auto* alloca =
        builder->createAlloca(context->getInt32Type(), nullptr, "test_var");

    // Test getFunction() - should return the function this instruction belongs
    // to
    EXPECT_EQ(alloca->getFunction(), function);

    // Test getModule() - should return the module that contains the function
    EXPECT_EQ(alloca->getModule(), module.get());

    // Test with instruction that has no parent
    auto* standalone =
        AllocaInst::Create(context->getInt32Type(), nullptr, "standalone");
    EXPECT_EQ(standalone->getFunction(), nullptr);
    EXPECT_EQ(standalone->getModule(), nullptr);

    delete standalone;
}

TEST_F(InstructionTest, InstructionInsertionMethods) {
    auto* inst1 =
        builder->createAlloca(context->getInt32Type(), nullptr, "var1");
    auto* inst2 =
        builder->createAlloca(context->getInt32Type(), nullptr, "var2");
    auto* inst3 = AllocaInst::Create(context->getInt32Type(), nullptr, "var3");

    // Test insertAfter
    inst3->insertAfter(inst1);

    // Check order: inst1, inst3, inst2
    auto it = bb->begin();
    EXPECT_EQ(*it, inst1);
    ++it;
    EXPECT_EQ(*it, inst3);
    ++it;
    EXPECT_EQ(*it, inst2);

    // Test moveAfter
    auto* inst4 = AllocaInst::Create(context->getInt32Type(), nullptr, "var4");
    bb->push_back(inst4);

    inst4->moveAfter(inst1);

    // Check order: inst1, inst4, inst3, inst2
    it = bb->begin();
    EXPECT_EQ(*it, inst1);
    ++it;
    EXPECT_EQ(*it, inst4);
    ++it;
    EXPECT_EQ(*it, inst3);
    ++it;
    EXPECT_EQ(*it, inst2);

    // Test insertAfter with null parent (should do nothing)
    auto* standalone =
        AllocaInst::Create(context->getInt32Type(), nullptr, "standalone");
    auto* other = AllocaInst::Create(context->getInt32Type(), nullptr, "other");
    other->insertAfter(standalone);
    EXPECT_EQ(other->getParent(), nullptr);

    delete standalone;
    delete other;
}

TEST_F(InstructionTest, CastInstructionUtilities) {
    auto* int32Ty = context->getInt32Type();
    auto* int1Ty = context->getInt1Type();
    auto* floatTy = context->getFloatType();
    auto* ptrTy = PointerType::get(int32Ty);

    // Test CastInst::isCastable
    EXPECT_TRUE(CastInst::isCastable(int32Ty, int32Ty));  // Same type
    EXPECT_TRUE(CastInst::isCastable(int32Ty, int1Ty));   // Integer to integer
    EXPECT_TRUE(CastInst::isCastable(floatTy, floatTy));  // Float to float
    EXPECT_TRUE(CastInst::isCastable(ptrTy, ptrTy));      // Pointer to pointer
    EXPECT_TRUE(CastInst::isCastable(int32Ty, ptrTy));    // Integer to pointer
    EXPECT_TRUE(CastInst::isCastable(ptrTy, int32Ty));    // Pointer to integer
    EXPECT_FALSE(CastInst::isCastable(
        int32Ty, floatTy));  // Integer to float (not directly castable)

    // Test CastInst::getCastOpcode
    EXPECT_EQ(CastInst::getCastOpcode(int32Ty, int1Ty),
              CastInst::Trunc);  // Truncation
    EXPECT_EQ(CastInst::getCastOpcode(int1Ty, int32Ty),
              CastInst::SExt);  // Sign extension
    EXPECT_EQ(CastInst::getCastOpcode(ptrTy, int32Ty),
              CastInst::PtrToInt);  // Pointer to int
    EXPECT_EQ(CastInst::getCastOpcode(int32Ty, ptrTy),
              CastInst::IntToPtr);  // Int to pointer
    EXPECT_EQ(CastInst::getCastOpcode(floatTy, int32Ty),
              CastInst::BitCast);  // Default case
}

TEST_F(InstructionTest, InstructionCloneMethods) {
    auto* int32Ty = context->getInt32Type();

    // Test UnaryOperator clone
    auto* val = builder->getInt32(42);
    auto* notInst = UnaryOperator::Create(Opcode::Not, val, "not_test");
    auto* clonedNot = notInst->clone();
    auto* clonedUnary = dyn_cast<UnaryOperator>(clonedNot);

    ASSERT_NE(clonedUnary, nullptr);
    EXPECT_EQ(clonedUnary->getOpcode(), Opcode::Not);
    EXPECT_EQ(clonedUnary->getOperand(), val);
    EXPECT_EQ(clonedUnary->getName(), "not_test");
    EXPECT_NE(clonedUnary, notInst);

    delete notInst;
    delete clonedNot;

    // Test BinaryOperator clone
    auto* lhs = builder->getInt32(10);
    auto* rhs = builder->getInt32(20);
    auto* addInst = BinaryOperator::Create(Opcode::Add, lhs, rhs, "add_test");
    auto* clonedAdd = addInst->clone();
    auto* clonedBinary = dyn_cast<BinaryOperator>(clonedAdd);

    ASSERT_NE(clonedBinary, nullptr);
    EXPECT_EQ(clonedBinary->getOpcode(), Opcode::Add);
    EXPECT_EQ(clonedBinary->getOperand1(), lhs);
    EXPECT_EQ(clonedBinary->getOperand2(), rhs);
    EXPECT_EQ(clonedBinary->getName(), "add_test");
    EXPECT_NE(clonedBinary, addInst);

    delete addInst;
    delete clonedAdd;

    // Test CmpInst clone
    auto* cmpInst =
        CmpInst::Create(Opcode::ICmpEQ, CmpInst::ICMP_EQ, lhs, rhs, "cmp_test");
    auto* clonedCmp = cmpInst->clone();
    auto* clonedCompare = dyn_cast<CmpInst>(clonedCmp);

    ASSERT_NE(clonedCompare, nullptr);
    EXPECT_EQ(clonedCompare->getOpcode(), Opcode::ICmpEQ);
    EXPECT_EQ(clonedCompare->getPredicate(), CmpInst::ICMP_EQ);
    EXPECT_EQ(clonedCompare->getOperand1(), lhs);
    EXPECT_EQ(clonedCompare->getOperand2(), rhs);
    EXPECT_EQ(clonedCompare->getName(), "cmp_test");
    EXPECT_NE(clonedCompare, cmpInst);

    delete cmpInst;
    delete clonedCmp;

    // Test AllocaInst clone
    auto* allocaInst = AllocaInst::Create(int32Ty, nullptr, "alloca_test");
    auto* clonedAlloca = allocaInst->clone();
    auto* clonedAllocaInst = dyn_cast<AllocaInst>(clonedAlloca);

    ASSERT_NE(clonedAllocaInst, nullptr);
    EXPECT_EQ(clonedAllocaInst->getAllocatedType(), int32Ty);
    EXPECT_EQ(clonedAllocaInst->getArraySize(), nullptr);
    EXPECT_EQ(clonedAllocaInst->getName(), "alloca_test");
    EXPECT_NE(clonedAllocaInst, allocaInst);

    delete allocaInst;
    delete clonedAlloca;

    // Test LoadInst clone
    auto* ptr = builder->createAlloca(int32Ty, nullptr, "ptr");
    auto* loadInst = LoadInst::Create(ptr, "load_test");
    auto* clonedLoad = loadInst->clone();
    auto* clonedLoadInst = dyn_cast<LoadInst>(clonedLoad);

    ASSERT_NE(clonedLoadInst, nullptr);
    EXPECT_EQ(clonedLoadInst->getPointerOperand(), ptr);
    EXPECT_EQ(clonedLoadInst->getName(), "load_test");
    EXPECT_NE(clonedLoadInst, loadInst);

    delete loadInst;
    delete clonedLoad;

    // Test StoreInst clone
    auto* storeInst = StoreInst::Create(val, ptr);
    auto* clonedStore = storeInst->clone();
    auto* clonedStoreInst = dyn_cast<StoreInst>(clonedStore);

    ASSERT_NE(clonedStoreInst, nullptr);
    EXPECT_EQ(clonedStoreInst->getValueOperand(), val);
    EXPECT_EQ(clonedStoreInst->getPointerOperand(), ptr);
    EXPECT_NE(clonedStoreInst, storeInst);

    delete storeInst;
    delete clonedStore;

    // Test SelectInst clone
    auto* cond = builder->getTrue();
    auto* trueVal = builder->getInt32(100);
    auto* falseVal = builder->getInt32(200);
    auto* selectInst =
        SelectInst::Create(cond, trueVal, falseVal, "select_test");
    auto* clonedSelect = selectInst->clone();
    auto* clonedSelectInst = dyn_cast<SelectInst>(clonedSelect);

    ASSERT_NE(clonedSelectInst, nullptr);
    EXPECT_EQ(clonedSelectInst->getCondition(), cond);
    EXPECT_EQ(clonedSelectInst->getTrueValue(), trueVal);
    EXPECT_EQ(clonedSelectInst->getFalseValue(), falseVal);
    EXPECT_EQ(clonedSelectInst->getName(), "select_test");
    EXPECT_NE(clonedSelectInst, selectInst);

    delete selectInst;
    delete clonedSelect;

    // Test CastInst clone
    auto* castInst =
        CastInst::Create(CastInst::Trunc, val, int32Ty, "cast_test");
    auto* clonedCast = castInst->clone();
    auto* clonedCastInst = dyn_cast<CastInst>(clonedCast);

    ASSERT_NE(clonedCastInst, nullptr);
    EXPECT_EQ(clonedCastInst->getCastOpcode(), CastInst::Trunc);
    EXPECT_EQ(clonedCastInst->getOperand(0), val);
    EXPECT_EQ(clonedCastInst->getDestType(), int32Ty);
    EXPECT_EQ(clonedCastInst->getName(), "cast_test");
    EXPECT_NE(clonedCastInst, castInst);

    delete castInst;
    delete clonedCast;

    // Test ReturnInst clone
    auto* retInst = ReturnInst::Create(context.get(), val);
    auto* clonedRet = retInst->clone();
    auto* clonedRetInst = dyn_cast<ReturnInst>(clonedRet);

    ASSERT_NE(clonedRetInst, nullptr);
    EXPECT_EQ(clonedRetInst->getReturnValue(), val);
    EXPECT_NE(clonedRetInst, retInst);

    delete retInst;
    delete clonedRet;
}

}  // namespace