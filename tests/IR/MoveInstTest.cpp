#include <gtest/gtest.h>

#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Module.h"

using namespace midend;

namespace {

class MoveInstTest : public ::testing::Test {
   protected:
    void SetUp() override {
        context = std::make_unique<Context>();
        module = std::make_unique<Module>("test", context.get());
    }

    std::unique_ptr<Context> context;
    std::unique_ptr<Module> module;
};

TEST_F(MoveInstTest, BasicMoveInstruction) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {});
    auto* func = Function::Create(fnTy, "test_move", module.get());
    auto* bb = BasicBlock::Create(context.get(), "entry", func);

    IRBuilder builder(bb);

    // Create a constant and assign it to a variable using Move instruction
    auto* const42 = builder.getInt32(42);
    auto* move = builder.createMove(const42, "a");

    // Return the moved value
    builder.createRet(move);

    // Test properties
    EXPECT_EQ(move->getOpcode(), Opcode::Move);
    EXPECT_EQ(move->getValue(), const42);
    EXPECT_EQ(move->getType(), int32Ty);
    EXPECT_EQ(move->getName(), "a");

    // Test that the move instruction is correctly formed
    EXPECT_EQ(move->getNumOperands(), 1u);
    EXPECT_EQ(move->getOperand(0), const42);

    // Test casting
    EXPECT_TRUE(isa<MoveInst>(*move));
    EXPECT_TRUE(isa<Instruction>(*move));
    EXPECT_TRUE(isa<User>(*move));
    EXPECT_TRUE(isa<Value>(*move));
}

TEST_F(MoveInstTest, MoveChain) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* func = Function::Create(fnTy, "test_move_chain", module.get());
    auto* bb = BasicBlock::Create(context.get(), "entry", func);

    IRBuilder builder(bb);

    // Create a chain of moves: arg -> a -> b -> c
    auto* arg = func->getArg(0);
    auto* a = builder.createMove(arg, "a");
    auto* b = builder.createMove(a, "b");
    auto* c = builder.createMove(b, "c");

    builder.createRet(c);

    // Test the chain
    EXPECT_EQ(a->getValue(), arg);
    EXPECT_EQ(b->getValue(), a);
    EXPECT_EQ(c->getValue(), b);

    // Test use-def relationships
    EXPECT_EQ(arg->getNumUses(), 1u);
    EXPECT_EQ(a->getNumUses(), 1u);
    EXPECT_EQ(b->getNumUses(), 1u);
    EXPECT_EQ(c->getNumUses(), 1u);  // Used by return
}

TEST_F(MoveInstTest, SimpleAssignmentExample) {
    // Demonstrate how "int a = 42;" would be represented
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {});
    auto* func = Function::Create(fnTy, "simple_assignment", module.get());
    auto* bb = BasicBlock::Create(context.get(), "entry", func);

    IRBuilder builder(bb);

    // int a = 42;  ->  %a = mov i32 42
    auto* const42 = builder.getInt32(42);
    auto* a = builder.createMove(const42, "a");

    // int b = a + 10;  ->  %b = add i32 %a, 10  (no move needed, add produces
    // result directly)
    auto* const10 = builder.getInt32(10);
    auto* b = builder.createAdd(a, const10, "b");

    // return b;
    builder.createRet(b);

    // Print the IR to see the result
    std::string ir = IRPrinter::toString(func);

    // Verify the structure
    EXPECT_TRUE(isa<MoveInst>(*a));
    EXPECT_TRUE(isa<BinaryOperator>(*b));
    EXPECT_FALSE(isa<MoveInst>(*b));  // Add directly produces its result
}

TEST_F(MoveInstTest, MoveInstCloning) {
    auto* int32Ty = context->getInt32Type();
    auto* const42 = ConstantInt::get(int32Ty, 42);
    auto* move = MoveInst::Create(const42, "original");

    auto* cloned = move->clone();
    auto* clonedMove = dyn_cast<MoveInst>(cloned);

    ASSERT_NE(clonedMove, nullptr);
    EXPECT_EQ(clonedMove->getValue(), const42);
    EXPECT_EQ(clonedMove->getName(), "original");
    EXPECT_NE(clonedMove, move);  // Different objects

    delete move;
    delete cloned;
}

TEST_F(MoveInstTest, MoveWithDifferentTypes) {
    // Test move with different value types
    auto* int32Ty = context->getInt32Type();
    auto* floatTy = context->getFloatType();

    // Integer move
    auto* intConst = ConstantInt::get(int32Ty, 100);
    auto* intMove = MoveInst::Create(intConst, "int_var");
    EXPECT_EQ(intMove->getType(), int32Ty);

    // Float move
    auto* floatConst = ConstantFP::get(floatTy, 3.14f);
    auto* floatMove = MoveInst::Create(floatConst, "float_var");
    EXPECT_EQ(floatMove->getType(), floatTy);

    delete intMove;
    delete floatMove;
}

}  // namespace