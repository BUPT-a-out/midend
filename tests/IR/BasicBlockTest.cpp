#include <gtest/gtest.h>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Instructions.h"
#include "IR/Module.h"

using namespace midend;

class BasicBlockTest : public ::testing::Test {
   protected:
    void SetUp() override {
        context = std::make_unique<Context>();
        module = std::make_unique<Module>("test", context.get());

        // Create a simple function for testing
        auto* voidTy = context->getVoidType();
        auto* fnTy = FunctionType::get(voidTy, {});
        function = Function::Create(fnTy, "test_func");
        module->push_back(function);
    }

    std::unique_ptr<Context> context;
    std::unique_ptr<Module> module;
    Function* function;
};

TEST_F(BasicBlockTest, BasicBlockCreation) {
    auto* bb = BasicBlock::Create(context.get(), "entry", function);

    EXPECT_EQ(bb->getName(), "entry");
    EXPECT_EQ(bb->getParent(), function);
    EXPECT_EQ(bb->getModule(), module.get());
    EXPECT_TRUE(bb->empty());
    EXPECT_EQ(bb->size(), 0u);

    // BasicBlock automatically adds itself to the function in constructor
}

TEST_F(BasicBlockTest, InstructionInsertion) {
    auto* bb = BasicBlock::Create(context.get(), "entry", function);

    IRBuilder builder(bb);

    // Add some instructions
    auto* alloca = builder.createAlloca(context->getInt32Type(), nullptr, "x");
    auto* store = builder.createStore(builder.getInt32(42), alloca);
    auto* load = builder.createLoad(alloca, "loaded");
    builder.createRetVoid();

    EXPECT_FALSE(bb->empty());
    EXPECT_EQ(bb->size(), 4u);

    // Check instruction order
    auto it = bb->begin();
    EXPECT_EQ(*it, alloca);
    ++it;
    EXPECT_EQ(*it, store);
    ++it;
    EXPECT_EQ(*it, load);
    ++it;
    EXPECT_TRUE((*it)->isTerminator());
}

TEST_F(BasicBlockTest, TerminatorHandling) {
    auto* bb = BasicBlock::Create(context.get(), "entry", function);

    IRBuilder builder(bb);

    // Initially no terminator
    EXPECT_EQ(bb->getTerminator(), nullptr);

    // Add a return instruction
    auto* ret = builder.createRetVoid();

    EXPECT_NE(bb->getTerminator(), nullptr);
    EXPECT_EQ(bb->getTerminator(), ret);
    EXPECT_TRUE(bb->getTerminator()->isTerminator());
}

TEST_F(BasicBlockTest, InstructionIteration) {
    auto* bb = BasicBlock::Create(context.get(), "entry", function);

    IRBuilder builder(bb);

    auto* alloca = builder.createAlloca(context->getInt32Type());
    auto* store = builder.createStore(builder.getInt32(42), alloca);
    auto* load = builder.createLoad(alloca);

    // Forward iteration
    std::vector<Instruction*> instructions;
    for (auto* inst : *bb) {
        instructions.push_back(inst);
    }

    EXPECT_EQ(instructions.size(), 3u);
    EXPECT_EQ(instructions[0], alloca);
    EXPECT_EQ(instructions[1], store);
    EXPECT_EQ(instructions[2], load);

    // Reverse iteration
    std::vector<Instruction*> reverseInstructions;
    for (auto it = bb->rbegin(); it != bb->rend(); ++it) {
        reverseInstructions.push_back(*it);
    }

    EXPECT_EQ(reverseInstructions.size(), 3u);
    EXPECT_EQ(reverseInstructions[0], load);
    EXPECT_EQ(reverseInstructions[1], store);
    EXPECT_EQ(reverseInstructions[2], alloca);
}

TEST_F(BasicBlockTest, InstructionRemoval) {
    auto* bb = BasicBlock::Create(context.get(), "entry", function);

    IRBuilder builder(bb);

    auto* alloca = builder.createAlloca(context->getInt32Type());
    auto* store = builder.createStore(builder.getInt32(42), alloca);
    auto* load = builder.createLoad(alloca);

    EXPECT_EQ(bb->size(), 3u);

    // Remove the store instruction
    bb->remove(store);
    delete store;

    EXPECT_EQ(bb->size(), 2u);

    auto it = bb->begin();
    EXPECT_EQ(*it, alloca);
    ++it;
    EXPECT_EQ(*it, load);
}

TEST_F(BasicBlockTest, BasicBlockMovement) {
    auto* bb1 = BasicBlock::Create(context.get(), "bb1", function);
    auto* bb2 = BasicBlock::Create(context.get(), "bb2", function);

    EXPECT_EQ(function->size(), 2u);

    auto it = function->begin();
    EXPECT_EQ(*it, bb1);
    ++it;
    EXPECT_EQ(*it, bb2);

    // Move bb2 before bb1
    bb2->moveBefore(bb1);

    it = function->begin();
    EXPECT_EQ(*it, bb2);
    ++it;
    EXPECT_EQ(*it, bb1);
}