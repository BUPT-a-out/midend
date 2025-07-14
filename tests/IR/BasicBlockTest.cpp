#include <gtest/gtest.h>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Module.h"

using namespace midend;

namespace {

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

TEST_F(BasicBlockTest, PredecessorCaching) {
    // Create a simple CFG with multiple blocks
    auto* bb1 = BasicBlock::Create(context.get(), "bb1", function);
    auto* bb2 = BasicBlock::Create(context.get(), "bb2", function);
    auto* bb3 = BasicBlock::Create(context.get(), "bb3", function);

    IRBuilder builder(bb1);

    // bb1 -> bb2 (unconditional branch)
    builder.createBr(bb2);

    // bb2 -> bb3 (unconditional branch)
    builder.setInsertPoint(bb2);
    builder.createBr(bb3);

    // bb3 -> return
    builder.setInsertPoint(bb3);
    builder.createRetVoid();

    // Test predecessor results
    auto preds1 = bb1->getPredecessors();
    auto preds2 = bb2->getPredecessors();
    auto preds3 = bb3->getPredecessors();

    EXPECT_EQ(preds1.size(), 0u);  // bb1 has no predecessors
    EXPECT_EQ(preds2.size(), 1u);  // bb2 has bb1 as predecessor
    EXPECT_EQ(preds3.size(), 1u);  // bb3 has bb2 as predecessor

    EXPECT_EQ(preds2[0], bb1);
    EXPECT_EQ(preds3[0], bb2);

    // Test cache consistency by calling getPredecessors multiple times
    auto preds2_again = bb2->getPredecessors();
    auto preds3_again = bb3->getPredecessors();

    EXPECT_EQ(preds2.size(), preds2_again.size());
    EXPECT_EQ(preds3.size(), preds3_again.size());
    EXPECT_EQ(preds2[0], preds2_again[0]);
    EXPECT_EQ(preds3[0], preds3_again[0]);
}

TEST_F(BasicBlockTest, PredecessorCacheInvalidation) {
    // Create a simple CFG
    auto* bb1 = BasicBlock::Create(context.get(), "bb1", function);
    auto* bb2 = BasicBlock::Create(context.get(), "bb2", function);
    auto* bb3 = BasicBlock::Create(context.get(), "bb3", function);

    IRBuilder builder(bb1);

    // Initially: bb1 -> bb2
    builder.createBr(bb2);

    builder.setInsertPoint(bb2);
    builder.createRetVoid();

    builder.setInsertPoint(bb3);
    builder.createRetVoid();

    // Test initial state
    auto preds2 = bb2->getPredecessors();
    auto preds3 = bb3->getPredecessors();

    EXPECT_EQ(preds2.size(), 1u);
    EXPECT_EQ(preds3.size(), 0u);
    EXPECT_EQ(preds2[0], bb1);

    // Modify the branch to target bb3 instead of bb2
    auto* br = dyn_cast<BranchInst>(bb1->getTerminator());
    ASSERT_NE(br, nullptr);

    br->setOperand(0, bb3);  // Change target from bb2 to bb3

    // Test cache invalidation - predecessors should be updated
    auto preds2_after = bb2->getPredecessors();
    auto preds3_after = bb3->getPredecessors();

    EXPECT_EQ(preds2_after.size(), 0u);  // bb2 no longer has predecessors
    EXPECT_EQ(preds3_after.size(), 1u);  // bb3 now has bb1 as predecessor
    EXPECT_EQ(preds3_after[0], bb1);
}

TEST_F(BasicBlockTest, PushBackAndPushFront) {
    auto* bb = BasicBlock::Create(context.get(), "entry", function);

    // Create instructions manually (not using IRBuilder)
    auto* int32Ty = context->getInt32Type();
    auto* alloca1 = AllocaInst::Create(int32Ty, nullptr, "var1");
    auto* alloca2 = AllocaInst::Create(int32Ty, nullptr, "var2");
    auto* alloca3 = AllocaInst::Create(int32Ty, nullptr, "var3");

    // Test push_back
    bb->push_back(alloca1);
    EXPECT_EQ(bb->size(), 1u);
    EXPECT_EQ(alloca1->getParent(), bb);
    EXPECT_EQ(*bb->begin(), alloca1);

    bb->push_back(alloca2);
    EXPECT_EQ(bb->size(), 2u);
    auto it = bb->begin();
    EXPECT_EQ(*it, alloca1);
    ++it;
    EXPECT_EQ(*it, alloca2);

    // Test push_front
    bb->push_front(alloca3);
    EXPECT_EQ(bb->size(), 3u);
    EXPECT_EQ(alloca3->getParent(), bb);

    // Check order: alloca3, alloca1, alloca2
    it = bb->begin();
    EXPECT_EQ(*it, alloca3);
    ++it;
    EXPECT_EQ(*it, alloca1);
    ++it;
    EXPECT_EQ(*it, alloca2);
}

TEST_F(BasicBlockTest, RemoveTerminatorInstruction) {
    auto* bb1 = BasicBlock::Create(context.get(), "bb1", function);
    auto* bb2 = BasicBlock::Create(context.get(), "bb2", function);

    IRBuilder builder(bb1);

    // Add a terminator instruction
    auto* br = builder.createBr(bb2);
    EXPECT_TRUE(br->isTerminator());
    EXPECT_EQ(bb1->getTerminator(), br);

    // Remove the terminator
    bb1->remove(br);
    EXPECT_EQ(bb1->getTerminator(), nullptr);
    EXPECT_EQ(br->getParent(), nullptr);

    delete br;
}

TEST_F(BasicBlockTest, InsertAfterFunction) {
    // Create a standalone basic block (not attached to function)
    auto* standaloneBB = BasicBlock::Create(context.get(), "standalone");
    EXPECT_EQ(standaloneBB->getParent(), nullptr);

    auto* bb1 = BasicBlock::Create(context.get(), "bb1", function);

    // Test insertAfter
    standaloneBB->insertAfter(bb1);
    EXPECT_EQ(standaloneBB->getParent(), function);

    // Check order in function
    auto it = function->begin();
    EXPECT_EQ(*it, bb1);
    ++it;
    EXPECT_EQ(*it, standaloneBB);
}

TEST_F(BasicBlockTest, MoveAfterFunction) {
    auto* bb1 = BasicBlock::Create(context.get(), "bb1", function);
    auto* bb2 = BasicBlock::Create(context.get(), "bb2", function);
    auto* bb3 = BasicBlock::Create(context.get(), "bb3", function);

    // Initial order: bb1, bb2, bb3
    auto it = function->begin();
    EXPECT_EQ(*it, bb1);
    ++it;
    EXPECT_EQ(*it, bb2);
    ++it;
    EXPECT_EQ(*it, bb3);

    // Move bb1 after bb3
    bb1->moveAfter(bb3);

    // New order: bb2, bb3, bb1
    it = function->begin();
    EXPECT_EQ(*it, bb2);
    ++it;
    EXPECT_EQ(*it, bb3);
    ++it;
    EXPECT_EQ(*it, bb1);
}

TEST_F(BasicBlockTest, EraseFromParent) {
    auto* bb1 = BasicBlock::Create(context.get(), "bb1", function);
    auto* bb2 = BasicBlock::Create(context.get(), "bb2", function);

    EXPECT_EQ(function->size(), 2u);

    // Erase bb1 from parent
    bb1->eraseFromParent();

    EXPECT_EQ(function->size(), 1u);
    EXPECT_EQ(*function->begin(), bb2);

    // bb1 should be deleted automatically by eraseFromParent
}

TEST_F(BasicBlockTest, GetPredecessorsWithoutParent) {
    // Create a standalone basic block (not attached to function)
    auto* standaloneBB = BasicBlock::Create(context.get(), "standalone");
    EXPECT_EQ(standaloneBB->getParent(), nullptr);

    // getPredecessors should return empty vector when no parent
    auto preds = standaloneBB->getPredecessors();
    EXPECT_EQ(preds.size(), 0u);

    delete standaloneBB;
}

}  // namespace