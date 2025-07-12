#include <gtest/gtest.h>

#include <algorithm>
#include <functional>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Analysis/DominanceInfo.h"

using namespace midend;

namespace {

class DominanceTest : public ::testing::Test {
   protected:
    std::unique_ptr<Context> context;
    std::unique_ptr<Module> module;

    void SetUp() override {
        context = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", context.get());
    }

    void TearDown() override {
        module.reset();
        context.reset();
    }

    // Create a simple linear function: entry -> bb1 -> bb2 -> exit
    Function* createLinearFunction() {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "linear", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* bb1 = BasicBlock::Create(context.get(), "bb1", func);
        auto* bb2 = BasicBlock::Create(context.get(), "bb2", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        builder.createBr(bb1);

        builder.setInsertPoint(bb1);
        builder.createBr(bb2);

        builder.setInsertPoint(bb2);
        builder.createBr(exit);

        builder.setInsertPoint(exit);
        builder.createRet(func->getArg(0));

        return func;
    }

    // Create a diamond-shaped CFG: entry -> left/right -> merge -> exit
    Function* createDiamondFunction() {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty});
        auto* func = Function::Create(fnTy, "diamond", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* left = BasicBlock::Create(context.get(), "left", func);
        auto* right = BasicBlock::Create(context.get(), "right", func);
        auto* merge = BasicBlock::Create(context.get(), "merge", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* cond =
            builder.createICmpSGT(func->getArg(0), func->getArg(1), "cond");
        builder.createCondBr(cond, left, right);

        builder.setInsertPoint(left);
        auto* leftVal =
            builder.createAdd(func->getArg(0), func->getArg(1), "left_val");
        builder.createBr(merge);

        builder.setInsertPoint(right);
        auto* rightVal =
            builder.createSub(func->getArg(0), func->getArg(1), "right_val");
        builder.createBr(merge);

        builder.setInsertPoint(merge);
        auto* phi = builder.createPHI(int32Ty, "result");
        phi->addIncoming(leftVal, left);
        phi->addIncoming(rightVal, right);
        builder.createBr(exit);

        builder.setInsertPoint(exit);
        builder.createRet(phi);

        return func;
    }

    // Create a loop: entry -> loop_header -> loop_body -> loop_header | exit
    Function* createLoopFunction() {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "loop", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* loop_header =
            BasicBlock::Create(context.get(), "loop_header", func);
        auto* loop_body = BasicBlock::Create(context.get(), "loop_body", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* initial = func->getArg(0);
        builder.createBr(loop_header);

        builder.setInsertPoint(loop_header);
        auto* phi = builder.createPHI(int32Ty, "counter");
        phi->addIncoming(initial, entry);
        auto* cond = builder.createICmpSGT(phi, builder.getInt32(0), "cond");
        builder.createCondBr(cond, loop_body, exit);

        builder.setInsertPoint(loop_body);
        auto* decremented = builder.createSub(phi, builder.getInt32(1), "dec");
        phi->addIncoming(decremented, loop_body);
        builder.createBr(loop_header);

        builder.setInsertPoint(exit);
        builder.createRet(phi);

        return func;
    }
};

TEST_F(DominanceTest, LinearCFGDominance) {
    auto* func = createLinearFunction();
    DominanceInfo domInfo(func);

    auto* entry = &func->front();
    auto blocks = func->getBasicBlocks();
    auto* bb1 = blocks[1];
    auto* bb2 = blocks[2];
    auto* exit = blocks[3];

    // Entry dominates everything
    EXPECT_TRUE(domInfo.dominates(entry, entry));
    EXPECT_TRUE(domInfo.dominates(entry, bb1));
    EXPECT_TRUE(domInfo.dominates(entry, bb2));
    EXPECT_TRUE(domInfo.dominates(entry, exit));

    // bb1 dominates bb2 and exit
    EXPECT_FALSE(domInfo.dominates(bb1, entry));
    EXPECT_TRUE(domInfo.dominates(bb1, bb1));
    EXPECT_TRUE(domInfo.dominates(bb1, bb2));
    EXPECT_TRUE(domInfo.dominates(bb1, exit));

    // bb2 dominates only exit and itself
    EXPECT_FALSE(domInfo.dominates(bb2, entry));
    EXPECT_FALSE(domInfo.dominates(bb2, bb1));
    EXPECT_TRUE(domInfo.dominates(bb2, bb2));
    EXPECT_TRUE(domInfo.dominates(bb2, exit));

    // exit dominates only itself
    EXPECT_FALSE(domInfo.dominates(exit, entry));
    EXPECT_FALSE(domInfo.dominates(exit, bb1));
    EXPECT_FALSE(domInfo.dominates(exit, bb2));
    EXPECT_TRUE(domInfo.dominates(exit, exit));

    // Check immediate dominators
    EXPECT_EQ(domInfo.getImmediateDominator(entry), nullptr);
    EXPECT_EQ(domInfo.getImmediateDominator(bb1), entry);
    EXPECT_EQ(domInfo.getImmediateDominator(bb2), bb1);
    EXPECT_EQ(domInfo.getImmediateDominator(exit), bb2);

    EXPECT_TRUE(domInfo.verify());
}

TEST_F(DominanceTest, DiamondCFGDominance) {
    auto* func = createDiamondFunction();
    DominanceInfo domInfo(func);

    auto blocks = func->getBasicBlocks();
    auto* entry = &func->front();
    BasicBlock* left = nullptr;
    BasicBlock* right = nullptr;
    BasicBlock* merge = nullptr;
    BasicBlock* exit = nullptr;

    for (auto* bb : blocks) {
        if (bb->getName() == "left")
            left = bb;
        else if (bb->getName() == "right")
            right = bb;
        else if (bb->getName() == "merge")
            merge = bb;
        else if (bb->getName() == "exit")
            exit = bb;
    }

    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    ASSERT_NE(merge, nullptr);
    ASSERT_NE(exit, nullptr);

    // Entry dominates everything
    EXPECT_TRUE(domInfo.dominates(entry, left));
    EXPECT_TRUE(domInfo.dominates(entry, right));
    EXPECT_TRUE(domInfo.dominates(entry, merge));
    EXPECT_TRUE(domInfo.dominates(entry, exit));

    // Left and right don't dominate each other
    EXPECT_FALSE(domInfo.dominates(left, right));
    EXPECT_FALSE(domInfo.dominates(right, left));

    // Neither left nor right dominates merge (both are predecessors)
    EXPECT_FALSE(domInfo.dominates(left, merge));
    EXPECT_FALSE(domInfo.dominates(right, merge));

    // Merge dominates exit
    EXPECT_TRUE(domInfo.dominates(merge, exit));

    // Check immediate dominators
    EXPECT_EQ(domInfo.getImmediateDominator(left), entry);
    EXPECT_EQ(domInfo.getImmediateDominator(right), entry);
    EXPECT_EQ(domInfo.getImmediateDominator(merge), entry);
    EXPECT_EQ(domInfo.getImmediateDominator(exit), merge);

    // Check dominance frontier
    const auto& leftDF = domInfo.getDominanceFrontier(left);
    const auto& rightDF = domInfo.getDominanceFrontier(right);

    EXPECT_TRUE(leftDF.count(merge) > 0);
    EXPECT_TRUE(rightDF.count(merge) > 0);

    EXPECT_TRUE(domInfo.verify());
}

TEST_F(DominanceTest, LoopCFGDominance) {
    auto* func = createLoopFunction();
    DominanceInfo domInfo(func);

    auto blocks = func->getBasicBlocks();
    auto* entry = &func->front();
    BasicBlock* loop_header = nullptr;
    BasicBlock* loop_body = nullptr;
    BasicBlock* exit = nullptr;

    for (auto* bb : blocks) {
        if (bb->getName() == "loop_header")
            loop_header = bb;
        else if (bb->getName() == "loop_body")
            loop_body = bb;
        else if (bb->getName() == "exit")
            exit = bb;
    }

    ASSERT_NE(loop_header, nullptr);
    ASSERT_NE(loop_body, nullptr);
    ASSERT_NE(exit, nullptr);

    // Entry dominates everything
    EXPECT_TRUE(domInfo.dominates(entry, loop_header));
    EXPECT_TRUE(domInfo.dominates(entry, loop_body));
    EXPECT_TRUE(domInfo.dominates(entry, exit));

    // Loop header dominates loop body and exit
    EXPECT_TRUE(domInfo.dominates(loop_header, loop_body));
    EXPECT_TRUE(domInfo.dominates(loop_header, exit));

    // Loop body doesn't dominate exit (there's a path from header to exit)
    EXPECT_FALSE(domInfo.dominates(loop_body, exit));

    // Check immediate dominators
    EXPECT_EQ(domInfo.getImmediateDominator(loop_header), entry);
    EXPECT_EQ(domInfo.getImmediateDominator(loop_body), loop_header);
    EXPECT_EQ(domInfo.getImmediateDominator(exit), loop_header);

    EXPECT_TRUE(domInfo.verify());
}

TEST_F(DominanceTest, DominatorTree) {
    auto* func = createDiamondFunction();
    DominanceInfo domInfo(func);

    auto* domTree = domInfo.getDominatorTree();
    ASSERT_NE(domTree, nullptr);

    auto* root = domTree->getRoot();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->bb->getName(), "entry");
    EXPECT_EQ(root->level, 0);

    // Check tree structure
    EXPECT_GE(root->children.size(), 1u);

    // Verify domination using tree
    auto blocks = func->getBasicBlocks();
    for (auto* bb1 : blocks) {
        for (auto* bb2 : blocks) {
            EXPECT_EQ(domInfo.dominates(bb1, bb2),
                      domTree->dominates(bb1, bb2));
        }
    }
}

TEST_F(DominanceTest, EmptyFunction) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {});
    auto* func = Function::Create(fnTy, "empty", module.get());

    DominanceInfo domInfo(func);
    EXPECT_TRUE(domInfo.verify());
}

TEST_F(DominanceTest, SingleBlockFunction) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* func = Function::Create(fnTy, "single", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    IRBuilder builder(entry);
    builder.createRet(func->getArg(0));

    DominanceInfo domInfo(func);

    EXPECT_TRUE(domInfo.dominates(entry, entry));
    EXPECT_EQ(domInfo.getImmediateDominator(entry), nullptr);
    EXPECT_TRUE(domInfo.getDominanceFrontier(entry).empty());

    EXPECT_TRUE(domInfo.verify());
}

TEST_F(DominanceTest, DominanceFrontier) {
    auto* func = createDiamondFunction();
    DominanceInfo domInfo(func);

    auto blocks = func->getBasicBlocks();
    BasicBlock* entry = &func->front();
    BasicBlock* left = nullptr;
    BasicBlock* right = nullptr;
    BasicBlock* merge = nullptr;

    for (auto* bb : blocks) {
        if (bb->getName() == "left")
            left = bb;
        else if (bb->getName() == "right")
            right = bb;
        else if (bb->getName() == "merge")
            merge = bb;
    }

    // In a diamond CFG, both left and right should have merge in their
    // dominance frontier
    const auto& leftDF = domInfo.getDominanceFrontier(left);
    const auto& rightDF = domInfo.getDominanceFrontier(right);

    EXPECT_TRUE(leftDF.count(merge) > 0);
    EXPECT_TRUE(rightDF.count(merge) > 0);

    // Entry should have empty dominance frontier (dominates everything)
    const auto& entryDF = domInfo.getDominanceFrontier(entry);
    EXPECT_TRUE(entryDF.empty());
}

TEST_F(DominanceTest, ComplexCFG) {
    // Create a more complex CFG with multiple join points
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty, int32Ty});
    auto* func = Function::Create(fnTy, "complex", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    auto* bb1 = BasicBlock::Create(context.get(), "bb1", func);
    auto* bb2 = BasicBlock::Create(context.get(), "bb2", func);
    auto* bb3 = BasicBlock::Create(context.get(), "bb3", func);
    auto* bb4 = BasicBlock::Create(context.get(), "bb4", func);
    auto* merge1 = BasicBlock::Create(context.get(), "merge1", func);
    auto* merge2 = BasicBlock::Create(context.get(), "merge2", func);
    auto* exit = BasicBlock::Create(context.get(), "exit", func);

    IRBuilder builder(entry);
    auto* cond1 =
        builder.createICmpSGT(func->getArg(0), func->getArg(1), "cond1");
    builder.createCondBr(cond1, bb1, bb2);

    builder.setInsertPoint(bb1);
    auto* cond2 =
        builder.createICmpSGT(func->getArg(1), func->getArg(2), "cond2");
    builder.createCondBr(cond2, bb3, bb4);

    builder.setInsertPoint(bb2);
    builder.createBr(merge1);

    builder.setInsertPoint(bb3);
    builder.createBr(merge1);

    builder.setInsertPoint(bb4);
    builder.createBr(merge2);

    builder.setInsertPoint(merge1);
    auto* phi1 = builder.createPHI(int32Ty, "phi1");
    phi1->addIncoming(func->getArg(0), bb2);
    phi1->addIncoming(func->getArg(1), bb3);
    builder.createBr(merge2);

    builder.setInsertPoint(merge2);
    auto* phi2 = builder.createPHI(int32Ty, "phi2");
    phi2->addIncoming(phi1, merge1);
    phi2->addIncoming(func->getArg(2), bb4);
    builder.createBr(exit);

    builder.setInsertPoint(exit);
    builder.createRet(phi2);

    // Debug: Check CFG connectivity
    std::cout << "CFG Debug:\n";
    for (auto& BB : *func) {
        std::cout << BB->getName() << " preds: ";
        auto preds = BB->getPredecessors();
        for (auto* pred : preds) {
            std::cout << pred->getName() << " ";
        }
        std::cout << "| succs: ";
        auto succs = BB->getSuccessors();
        for (auto* succ : succs) {
            std::cout << succ->getName() << " ";
        }
        std::cout << "\n";
    }

    DominanceInfo domInfo(func);
    EXPECT_TRUE(domInfo.verify());

    // Entry should dominate everything
    for (auto& BB : *func) {
        EXPECT_TRUE(domInfo.dominates(entry, BB));
    }

    // bb1 should dominate bb3, bb4 but not bb2, merge1
    EXPECT_TRUE(domInfo.dominates(bb1, bb3));
    EXPECT_TRUE(domInfo.dominates(bb1, bb4));
    EXPECT_FALSE(domInfo.dominates(bb1, bb2));
    EXPECT_FALSE(domInfo.dominates(bb1, merge1));
}

TEST_F(DominanceTest, DominanceAnalysisPass) {
    auto* func = createDiamondFunction();

    DominanceAnalysis analysis;
    auto result = analysis.runOnFunction(*func);
    ASSERT_NE(result, nullptr);

    auto* domInfo = dynamic_cast<DominanceInfo*>(result.get());
    ASSERT_NE(domInfo, nullptr);

    EXPECT_TRUE(domInfo->verify());
    EXPECT_EQ(domInfo->getFunction(), func);
}

TEST_F(DominanceTest, NestedLoopsWithMultipleBackEdges) {
    // Create nested loops: entry -> outer_header -> inner_header -> inner_body
    // -> inner_header | outer_body -> outer_header | exit
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* func = Function::Create(fnTy, "nested_loops", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    auto* outer_header =
        BasicBlock::Create(context.get(), "outer_header", func);
    auto* inner_header =
        BasicBlock::Create(context.get(), "inner_header", func);
    auto* inner_body = BasicBlock::Create(context.get(), "inner_body", func);
    auto* outer_body = BasicBlock::Create(context.get(), "outer_body", func);
    auto* exit = BasicBlock::Create(context.get(), "exit", func);

    IRBuilder builder(entry);
    builder.createBr(outer_header);

    builder.setInsertPoint(outer_header);
    auto* outerPhi = builder.createPHI(int32Ty, "outer_counter");
    outerPhi->addIncoming(func->getArg(0), entry);
    auto* outerCond =
        builder.createICmpSGT(outerPhi, builder.getInt32(0), "outer_cond");
    builder.createCondBr(outerCond, inner_header, exit);

    builder.setInsertPoint(inner_header);
    auto* innerPhi = builder.createPHI(int32Ty, "inner_counter");
    innerPhi->addIncoming(builder.getInt32(10), outer_header);
    auto* innerCond =
        builder.createICmpSGT(innerPhi, builder.getInt32(0), "inner_cond");
    builder.createCondBr(innerCond, inner_body, outer_body);

    builder.setInsertPoint(inner_body);
    auto* innerDec =
        builder.createSub(innerPhi, builder.getInt32(1), "inner_dec");
    innerPhi->addIncoming(innerDec, inner_body);
    builder.createBr(inner_header);

    builder.setInsertPoint(outer_body);
    auto* outerDec =
        builder.createSub(outerPhi, builder.getInt32(1), "outer_dec");
    outerPhi->addIncoming(outerDec, outer_body);
    builder.createBr(outer_header);

    builder.setInsertPoint(exit);
    builder.createRet(outerPhi);

    DominanceInfo domInfo(func);
    EXPECT_TRUE(domInfo.verify());

    // Verify dominance relationships
    EXPECT_TRUE(domInfo.dominates(entry, outer_header));
    EXPECT_TRUE(domInfo.dominates(outer_header, inner_header));
    EXPECT_TRUE(domInfo.dominates(inner_header, inner_body));
    EXPECT_TRUE(domInfo.dominates(outer_header, outer_body));
    EXPECT_TRUE(domInfo.dominates(outer_header, exit));

    // Inner body should not dominate outer body (can reach outer_body through
    // inner_header)
    EXPECT_FALSE(domInfo.dominates(inner_body, outer_body));

    // Check immediate dominators
    EXPECT_EQ(domInfo.getImmediateDominator(outer_header), entry);
    EXPECT_EQ(domInfo.getImmediateDominator(inner_header), outer_header);
    EXPECT_EQ(domInfo.getImmediateDominator(inner_body), inner_header);
    EXPECT_EQ(domInfo.getImmediateDominator(outer_body), inner_header);
    EXPECT_EQ(domInfo.getImmediateDominator(exit), outer_header);

    // Check dominance frontiers for loop headers
    const auto& outerDF = domInfo.getDominanceFrontier(outer_body);
    const auto& innerDF = domInfo.getDominanceFrontier(inner_body);
    EXPECT_TRUE(outerDF.count(outer_header) >
                0);  // outer_body -> outer_header is a back edge
    EXPECT_TRUE(innerDF.count(inner_header) >
                0);  // inner_body -> inner_header is a back edge
}

TEST_F(DominanceTest, IrreducibleCFG) {
    // Create an irreducible CFG with multiple entry points to a loop
    // entry -> bb1 -> bb3 -> bb4 -> bb1
    //       -> bb2 -> bb3
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* func = Function::Create(fnTy, "irreducible", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    auto* bb1 = BasicBlock::Create(context.get(), "bb1", func);
    auto* bb2 = BasicBlock::Create(context.get(), "bb2", func);
    auto* bb3 = BasicBlock::Create(context.get(), "bb3", func);
    auto* bb4 = BasicBlock::Create(context.get(), "bb4", func);
    auto* exit = BasicBlock::Create(context.get(), "exit", func);

    IRBuilder builder(entry);
    auto* cond =
        builder.createICmpSGT(func->getArg(0), builder.getInt32(0), "cond");
    builder.createCondBr(cond, bb1, bb2);

    builder.setInsertPoint(bb1);
    auto* phi1 = builder.createPHI(int32Ty, "phi1");
    phi1->addIncoming(func->getArg(0), entry);
    builder.createBr(bb3);

    builder.setInsertPoint(bb2);
    builder.createBr(bb3);

    builder.setInsertPoint(bb3);
    auto* phi3 = builder.createPHI(int32Ty, "phi3");
    phi3->addIncoming(phi1, bb1);
    phi3->addIncoming(func->getArg(0), bb2);
    phi3->addIncoming(builder.getInt32(42), bb4);
    auto* loopCond =
        builder.createICmpSGT(phi3, builder.getInt32(10), "loop_cond");
    builder.createCondBr(loopCond, bb4, exit);

    builder.setInsertPoint(bb4);
    auto* dec = builder.createSub(phi3, builder.getInt32(1), "dec");
    phi1->addIncoming(dec, bb4);
    auto* backEdgeCond =
        builder.createICmpSGT(dec, builder.getInt32(5), "back_edge_cond");
    builder.createCondBr(backEdgeCond, bb1, bb3);

    builder.setInsertPoint(exit);
    builder.createRet(phi3);

    DominanceInfo domInfo(func);
    EXPECT_TRUE(domInfo.verify());

    // Entry dominates everything
    EXPECT_TRUE(domInfo.dominates(entry, bb1));
    EXPECT_TRUE(domInfo.dominates(entry, bb2));
    EXPECT_TRUE(domInfo.dominates(entry, bb3));
    EXPECT_TRUE(domInfo.dominates(entry, bb4));
    EXPECT_TRUE(domInfo.dominates(entry, exit));

    // Neither bb1 nor bb2 dominates bb3 (two paths to reach bb3)
    EXPECT_FALSE(domInfo.dominates(bb1, bb3));
    EXPECT_FALSE(domInfo.dominates(bb2, bb3));

    // bb3 dominates bb4 and exit
    EXPECT_TRUE(domInfo.dominates(bb3, bb4));
    EXPECT_TRUE(domInfo.dominates(bb3, exit));

    // Check immediate dominators
    EXPECT_EQ(domInfo.getImmediateDominator(bb1), entry);
    EXPECT_EQ(domInfo.getImmediateDominator(bb2), entry);
    EXPECT_EQ(domInfo.getImmediateDominator(bb3), entry);
    EXPECT_EQ(domInfo.getImmediateDominator(bb4), bb3);
    EXPECT_EQ(domInfo.getImmediateDominator(exit), bb3);

    // Check dominance frontiers - bb3 should be in frontier of bb1 and bb2
    const auto& bb1DF = domInfo.getDominanceFrontier(bb1);
    const auto& bb2DF = domInfo.getDominanceFrontier(bb2);
    const auto& bb4DF = domInfo.getDominanceFrontier(bb4);
    EXPECT_TRUE(bb1DF.count(bb3) > 0);
    EXPECT_TRUE(bb2DF.count(bb3) > 0);
    EXPECT_TRUE(bb4DF.count(bb1) > 0);  // bb4 -> bb1 back edge
    EXPECT_TRUE(bb4DF.count(bb3) > 0);  // bb4 -> bb3 back edge
}

TEST_F(DominanceTest, GetDominatedAndStrictlyDominates) {
    auto* func = createDiamondFunction();
    DominanceInfo domInfo(func);

    auto blocks = func->getBasicBlocks();
    auto* entry = &func->front();
    BasicBlock* left = nullptr;
    BasicBlock* right = nullptr;
    BasicBlock* merge = nullptr;
    BasicBlock* exit = nullptr;

    for (auto* bb : blocks) {
        if (bb->getName() == "left")
            left = bb;
        else if (bb->getName() == "right")
            right = bb;
        else if (bb->getName() == "merge")
            merge = bb;
        else if (bb->getName() == "exit")
            exit = bb;
    }

    // Test getDominated() function
    const auto& entryDominated = domInfo.getDominated(entry);
    EXPECT_EQ(entryDominated.size(), 5u);  // entry dominates all 5 blocks
    EXPECT_TRUE(entryDominated.count(entry) > 0);
    EXPECT_TRUE(entryDominated.count(left) > 0);
    EXPECT_TRUE(entryDominated.count(right) > 0);
    EXPECT_TRUE(entryDominated.count(merge) > 0);
    EXPECT_TRUE(entryDominated.count(exit) > 0);

    const auto& leftDominated = domInfo.getDominated(left);
    EXPECT_EQ(leftDominated.size(), 1u);  // left only dominates itself
    EXPECT_TRUE(leftDominated.count(left) > 0);

    const auto& mergeDominated = domInfo.getDominated(merge);
    EXPECT_EQ(mergeDominated.size(), 2u);  // merge dominates itself and exit
    EXPECT_TRUE(mergeDominated.count(merge) > 0);
    EXPECT_TRUE(mergeDominated.count(exit) > 0);

    // Test strictlyDominates() function
    // Entry strictly dominates all blocks except itself
    EXPECT_FALSE(domInfo.strictlyDominates(entry, entry));
    EXPECT_TRUE(domInfo.strictlyDominates(entry, left));
    EXPECT_TRUE(domInfo.strictlyDominates(entry, right));
    EXPECT_TRUE(domInfo.strictlyDominates(entry, merge));
    EXPECT_TRUE(domInfo.strictlyDominates(entry, exit));

    // Left strictly dominates nothing
    EXPECT_FALSE(domInfo.strictlyDominates(left, left));
    EXPECT_FALSE(domInfo.strictlyDominates(left, right));
    EXPECT_FALSE(domInfo.strictlyDominates(left, merge));
    EXPECT_FALSE(domInfo.strictlyDominates(left, exit));

    // Merge strictly dominates only exit
    EXPECT_FALSE(domInfo.strictlyDominates(merge, merge));
    EXPECT_FALSE(domInfo.strictlyDominates(merge, left));
    EXPECT_FALSE(domInfo.strictlyDominates(merge, right));
    EXPECT_TRUE(domInfo.strictlyDominates(merge, exit));

    // Test null pointer handling
    EXPECT_FALSE(domInfo.strictlyDominates(nullptr, entry));
    EXPECT_FALSE(domInfo.strictlyDominates(entry, nullptr));
    EXPECT_FALSE(domInfo.dominates(nullptr, entry));
    EXPECT_FALSE(domInfo.dominates(entry, nullptr));
}

TEST_F(DominanceTest, DominatorTreeLCA) {
    // Create a complex CFG to test LCA
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* func = Function::Create(fnTy, "lca_test", module.get());

    // CFG structure:
    // entry -> a -> b -> d
    //       -> c -> d
    //            -> e -> f
    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    auto* a = BasicBlock::Create(context.get(), "a", func);
    auto* b = BasicBlock::Create(context.get(), "b", func);
    auto* c = BasicBlock::Create(context.get(), "c", func);
    auto* d = BasicBlock::Create(context.get(), "d", func);
    auto* e = BasicBlock::Create(context.get(), "e", func);
    auto* f = BasicBlock::Create(context.get(), "f", func);
    auto* exit = BasicBlock::Create(context.get(), "exit", func);

    IRBuilder builder(entry);
    auto* cond1 =
        builder.createICmpSGT(func->getArg(0), builder.getInt32(10), "cond1");
    builder.createCondBr(cond1, a, c);

    builder.setInsertPoint(a);
    builder.createBr(b);

    builder.setInsertPoint(b);
    builder.createBr(d);

    builder.setInsertPoint(c);
    auto* cond2 =
        builder.createICmpSGT(func->getArg(0), builder.getInt32(5), "cond2");
    builder.createCondBr(cond2, d, e);

    builder.setInsertPoint(d);
    auto* phi = builder.createPHI(int32Ty, "phi");
    phi->addIncoming(builder.getInt32(1), b);
    phi->addIncoming(builder.getInt32(2), c);
    builder.createBr(exit);

    builder.setInsertPoint(e);
    builder.createBr(f);

    builder.setInsertPoint(f);
    builder.createBr(exit);

    builder.setInsertPoint(exit);
    auto* result = builder.createPHI(int32Ty, "result");
    result->addIncoming(phi, d);
    result->addIncoming(builder.getInt32(3), f);
    builder.createRet(result);

    DominanceInfo domInfo(func);
    auto* domTree = domInfo.getDominatorTree();
    ASSERT_NE(domTree, nullptr);

    // Test LCA functionality
    auto* lcaAB = domTree->findLCA(a, b);
    EXPECT_EQ(lcaAB->bb, a);  // a dominates b

    auto* lcaAC = domTree->findLCA(a, c);
    EXPECT_EQ(lcaAC->bb, entry);  // entry is the common dominator

    auto* lcaBD = domTree->findLCA(b, d);
    EXPECT_EQ(lcaBD->bb, entry);  // entry is the common dominator

    auto* lcaDE = domTree->findLCA(d, e);
    EXPECT_EQ(lcaDE->bb, entry);  // entry is the common dominator

    auto* lcaEF = domTree->findLCA(e, f);
    EXPECT_EQ(lcaEF->bb, e);  // e dominates f

    auto* lcaCE = domTree->findLCA(c, e);
    EXPECT_EQ(lcaCE->bb, c);  // c dominates e

    // Test with same node
    auto* lcaAA = domTree->findLCA(a, a);
    EXPECT_EQ(lcaAA->bb, a);

    // Test null cases
    BasicBlock* nonExistent =
        BasicBlock::Create(context.get(), "nonexistent", func);
    auto* lcaNull = domTree->findLCA(a, nonExistent);
    EXPECT_EQ(lcaNull, nullptr);
}

TEST_F(DominanceTest, DominatorTreeGetNodesAtLevel) {
    auto* func = createDiamondFunction();
    DominanceInfo domInfo(func);
    auto* domTree = domInfo.getDominatorTree();
    ASSERT_NE(domTree, nullptr);

    // Level 0: Only entry
    auto level0 = domTree->getNodesAtLevel(0);
    EXPECT_EQ(level0.size(), 1u);
    EXPECT_EQ(level0[0]->bb->getName(), "entry");

    // Level 1: left, right, merge (all immediate children of entry)
    auto level1 = domTree->getNodesAtLevel(1);
    EXPECT_EQ(level1.size(), 3u);
    std::set<std::string> level1Names;
    for (auto* node : level1) {
        level1Names.insert(node->bb->getName());
    }
    EXPECT_TRUE(level1Names.count("left") > 0);
    EXPECT_TRUE(level1Names.count("right") > 0);
    EXPECT_TRUE(level1Names.count("merge") > 0);

    // Level 2: Only exit (child of merge)
    auto level2 = domTree->getNodesAtLevel(2);
    EXPECT_EQ(level2.size(), 1u);
    EXPECT_EQ(level2[0]->bb->getName(), "exit");

    // Level 3: Empty
    auto level3 = domTree->getNodesAtLevel(3);
    EXPECT_TRUE(level3.empty());

    // Test with complex nested structure
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* complexFunc = Function::Create(fnTy, "complex_levels", module.get());

    // Create a deeper tree structure
    auto* entry = BasicBlock::Create(context.get(), "entry", complexFunc);
    auto* bb1 = BasicBlock::Create(context.get(), "bb1", complexFunc);
    auto* bb2 = BasicBlock::Create(context.get(), "bb2", complexFunc);
    auto* bb3 = BasicBlock::Create(context.get(), "bb3", complexFunc);
    auto* bb4 = BasicBlock::Create(context.get(), "bb4", complexFunc);
    auto* bb5 = BasicBlock::Create(context.get(), "bb5", complexFunc);
    auto* exit = BasicBlock::Create(context.get(), "exit", complexFunc);

    IRBuilder builder(entry);
    builder.createBr(bb1);

    builder.setInsertPoint(bb1);
    auto* cond = builder.createICmpSGT(complexFunc->getArg(0),
                                       builder.getInt32(0), "cond");
    builder.createCondBr(cond, bb2, bb3);

    builder.setInsertPoint(bb2);
    builder.createBr(bb4);

    builder.setInsertPoint(bb3);
    builder.createBr(bb5);

    builder.setInsertPoint(bb4);
    builder.createBr(exit);

    builder.setInsertPoint(bb5);
    builder.createBr(exit);

    builder.setInsertPoint(exit);
    auto* phi = builder.createPHI(int32Ty, "result");
    phi->addIncoming(builder.getInt32(1), bb4);
    phi->addIncoming(builder.getInt32(2), bb5);
    builder.createRet(phi);

    DominanceInfo complexDomInfo(complexFunc);
    auto* complexDomTree = complexDomInfo.getDominatorTree();

    // Verify levels
    auto complexLevel0 = complexDomTree->getNodesAtLevel(0);
    EXPECT_EQ(complexLevel0.size(), 1u);  // entry

    auto complexLevel1 = complexDomTree->getNodesAtLevel(1);
    EXPECT_EQ(complexLevel1.size(), 1u);  // bb1

    auto complexLevel2 = complexDomTree->getNodesAtLevel(2);
    EXPECT_EQ(complexLevel2.size(), 3u);  // bb2, bb3, exit

    auto complexLevel3 = complexDomTree->getNodesAtLevel(3);
    EXPECT_EQ(complexLevel3.size(), 2u);  // bb4, bb5
}

TEST_F(DominanceTest, NestedDiamondPatterns) {
    // Create multiple nested diamond patterns
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty, int32Ty});
    auto* func = Function::Create(fnTy, "nested_diamonds", module.get());

    // Outer diamond
    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    auto* outerLeft = BasicBlock::Create(context.get(), "outer_left", func);
    auto* outerRight = BasicBlock::Create(context.get(), "outer_right", func);

    // Inner diamonds
    auto* innerLeft1 = BasicBlock::Create(context.get(), "inner_left1", func);
    auto* innerRight1 = BasicBlock::Create(context.get(), "inner_right1", func);
    auto* innerMerge1 = BasicBlock::Create(context.get(), "inner_merge1", func);

    auto* innerLeft2 = BasicBlock::Create(context.get(), "inner_left2", func);
    auto* innerRight2 = BasicBlock::Create(context.get(), "inner_right2", func);
    auto* innerMerge2 = BasicBlock::Create(context.get(), "inner_merge2", func);

    auto* outerMerge = BasicBlock::Create(context.get(), "outer_merge", func);
    auto* exit = BasicBlock::Create(context.get(), "exit", func);

    IRBuilder builder(entry);
    auto* outerCond =
        builder.createICmpSGT(func->getArg(0), func->getArg(1), "outer_cond");
    builder.createCondBr(outerCond, outerLeft, outerRight);

    // Left path: nested diamond
    builder.setInsertPoint(outerLeft);
    auto* innerCond1 =
        builder.createICmpSGT(func->getArg(1), func->getArg(2), "inner_cond1");
    builder.createCondBr(innerCond1, innerLeft1, innerRight1);

    builder.setInsertPoint(innerLeft1);
    auto* val1 = builder.createAdd(func->getArg(0), func->getArg(1), "val1");
    builder.createBr(innerMerge1);

    builder.setInsertPoint(innerRight1);
    auto* val2 = builder.createSub(func->getArg(0), func->getArg(1), "val2");
    builder.createBr(innerMerge1);

    builder.setInsertPoint(innerMerge1);
    auto* phi1 = builder.createPHI(int32Ty, "phi1");
    phi1->addIncoming(val1, innerLeft1);
    phi1->addIncoming(val2, innerRight1);
    builder.createBr(outerMerge);

    // Right path: another nested diamond
    builder.setInsertPoint(outerRight);
    auto* innerCond2 =
        builder.createICmpSGT(func->getArg(0), func->getArg(2), "inner_cond2");
    builder.createCondBr(innerCond2, innerLeft2, innerRight2);

    builder.setInsertPoint(innerLeft2);
    auto* val3 = builder.createMul(func->getArg(1), func->getArg(2), "val3");
    builder.createBr(innerMerge2);

    builder.setInsertPoint(innerRight2);
    auto* val4 = builder.createAdd(func->getArg(1), func->getArg(2), "val4");
    builder.createBr(innerMerge2);

    builder.setInsertPoint(innerMerge2);
    auto* phi2 = builder.createPHI(int32Ty, "phi2");
    phi2->addIncoming(val3, innerLeft2);
    phi2->addIncoming(val4, innerRight2);
    builder.createBr(outerMerge);

    builder.setInsertPoint(outerMerge);
    auto* outerPhi = builder.createPHI(int32Ty, "outer_phi");
    outerPhi->addIncoming(phi1, innerMerge1);
    outerPhi->addIncoming(phi2, innerMerge2);
    builder.createBr(exit);

    builder.setInsertPoint(exit);
    builder.createRet(outerPhi);

    DominanceInfo domInfo(func);
    EXPECT_TRUE(domInfo.verify());

    // Verify outer diamond dominance
    EXPECT_TRUE(domInfo.dominates(entry, outerLeft));
    EXPECT_TRUE(domInfo.dominates(entry, outerRight));
    EXPECT_TRUE(domInfo.dominates(entry, outerMerge));
    EXPECT_FALSE(domInfo.dominates(outerLeft, outerRight));
    EXPECT_FALSE(domInfo.dominates(outerRight, outerLeft));

    // Verify inner diamonds dominance
    EXPECT_TRUE(domInfo.dominates(outerLeft, innerLeft1));
    EXPECT_TRUE(domInfo.dominates(outerLeft, innerRight1));
    EXPECT_TRUE(domInfo.dominates(outerLeft, innerMerge1));
    EXPECT_FALSE(domInfo.dominates(innerLeft1, innerRight1));

    EXPECT_TRUE(domInfo.dominates(outerRight, innerLeft2));
    EXPECT_TRUE(domInfo.dominates(outerRight, innerRight2));
    EXPECT_TRUE(domInfo.dominates(outerRight, innerMerge2));
    EXPECT_FALSE(domInfo.dominates(innerLeft2, innerRight2));

    // Cross-diamond checks
    EXPECT_FALSE(domInfo.dominates(outerLeft, innerLeft2));
    EXPECT_FALSE(domInfo.dominates(outerRight, innerLeft1));
    EXPECT_FALSE(domInfo.dominates(innerMerge1, innerMerge2));

    // Check immediate dominators
    EXPECT_EQ(domInfo.getImmediateDominator(outerLeft), entry);
    EXPECT_EQ(domInfo.getImmediateDominator(outerRight), entry);
    EXPECT_EQ(domInfo.getImmediateDominator(innerLeft1), outerLeft);
    EXPECT_EQ(domInfo.getImmediateDominator(innerRight1), outerLeft);
    EXPECT_EQ(domInfo.getImmediateDominator(innerMerge1), outerLeft);
    EXPECT_EQ(domInfo.getImmediateDominator(innerLeft2), outerRight);
    EXPECT_EQ(domInfo.getImmediateDominator(innerRight2), outerRight);
    EXPECT_EQ(domInfo.getImmediateDominator(innerMerge2), outerRight);
    EXPECT_EQ(domInfo.getImmediateDominator(outerMerge), entry);

    // Check dominance frontiers
    const auto& innerLeft1DF = domInfo.getDominanceFrontier(innerLeft1);
    const auto& innerRight1DF = domInfo.getDominanceFrontier(innerRight1);
    const auto& innerMerge1DF = domInfo.getDominanceFrontier(innerMerge1);

    EXPECT_TRUE(innerLeft1DF.count(innerMerge1) > 0);
    EXPECT_TRUE(innerRight1DF.count(innerMerge1) > 0);
    EXPECT_TRUE(innerMerge1DF.count(outerMerge) > 0);

    // Check dominator tree structure
    auto* domTree = domInfo.getDominatorTree();
    auto* rootNode = domTree->getRoot();
    EXPECT_EQ(rootNode->bb, entry);

    // Verify tree levels
    auto level0 = domTree->getNodesAtLevel(0);
    EXPECT_EQ(level0.size(), 1u);  // entry

    auto level1 = domTree->getNodesAtLevel(1);
    EXPECT_EQ(level1.size(), 3u);  // outerLeft, outerRight, outerMerge

    auto level2 = domTree->getNodesAtLevel(2);
    EXPECT_EQ(level2.size(), 7u);  // innerLeft1, innerRight1, innerMerge1,
                                   // innerLeft2, innerRight2, innerMerge2, exit
}

TEST_F(DominanceTest, ReversePostOrderTraversal) {
    // Test RPO traversal with a simple linear CFG
    auto* func = createLinearFunction();
    DominanceInfo domInfo(func);

    auto rpo = domInfo.computeReversePostOrder();
    auto blocks = func->getBasicBlocks();

    // For a linear CFG: entry -> bb1 -> bb2 -> exit
    // RPO should be: entry, bb1, bb2, exit (same as program order)
    ASSERT_EQ(rpo.size(), 4u);
    EXPECT_EQ(rpo[0]->getName(), "entry");
    EXPECT_EQ(rpo[1]->getName(), "bb1");
    EXPECT_EQ(rpo[2]->getName(), "bb2");
    EXPECT_EQ(rpo[3]->getName(), "exit");

    // Verify RPO properties: all predecessors come before successors (except
    // back edges)
    for (size_t i = 0; i < rpo.size(); ++i) {
        auto predecessors = rpo[i]->getPredecessors();
        for (auto* pred : predecessors) {
            // Find position of predecessor in RPO
            auto predPos = std::find(rpo.begin(), rpo.end(), pred);
            EXPECT_NE(predPos, rpo.end()) << "Predecessor not found in RPO";
            if (predPos != rpo.end()) {
                size_t predIndex = predPos - rpo.begin();
                // Predecessor should come before current block (no back edges
                // in linear CFG)
                EXPECT_LT(predIndex, i)
                    << "Predecessor should come before current block in RPO";
            }
        }
    }
}

TEST_F(DominanceTest, RPOWithComplexCFG) {
    // Test RPO traversal with a diamond CFG
    auto* func = createDiamondFunction();
    DominanceInfo domInfo(func);

    auto rpo = domInfo.computeReversePostOrder();

    // Find blocks by name
    BasicBlock* entry = &func->front();
    BasicBlock* left = nullptr;
    BasicBlock* right = nullptr;
    BasicBlock* merge = nullptr;
    BasicBlock* exit = nullptr;

    for (auto* bb : rpo) {
        if (bb->getName() == "left")
            left = bb;
        else if (bb->getName() == "right")
            right = bb;
        else if (bb->getName() == "merge")
            merge = bb;
        else if (bb->getName() == "exit")
            exit = bb;
    }

    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    ASSERT_NE(merge, nullptr);
    ASSERT_NE(exit, nullptr);

    // For diamond CFG: entry -> {left, right} -> merge -> exit
    // Valid RPO orderings: entry, left, right, merge, exit OR entry, right,
    // left, merge, exit
    ASSERT_EQ(rpo.size(), 5u);
    EXPECT_EQ(rpo[0], entry);  // Entry must be first
    EXPECT_EQ(rpo[4], exit);   // Exit must be last
    EXPECT_EQ(rpo[3], merge);  // Merge must be before exit

    // left and right can be in either order, but both must be after entry and
    // before merge
    size_t leftPos = std::find(rpo.begin(), rpo.end(), left) - rpo.begin();
    size_t rightPos = std::find(rpo.begin(), rpo.end(), right) - rpo.begin();
    size_t mergePos = std::find(rpo.begin(), rpo.end(), merge) - rpo.begin();

    EXPECT_LT(leftPos, mergePos);
    EXPECT_LT(rightPos, mergePos);
    EXPECT_GT(leftPos, 0u);   // After entry
    EXPECT_GT(rightPos, 0u);  // After entry
}

TEST_F(DominanceTest, RPOWithBackEdges) {
    // Test RPO with a loop that has back edges
    auto* func = createLoopFunction();
    DominanceInfo domInfo(func);

    auto rpo = domInfo.computeReversePostOrder();

    // Find blocks by name
    auto* entry = &func->front();
    BasicBlock* loop_header = nullptr;
    BasicBlock* loop_body = nullptr;
    BasicBlock* exit = nullptr;

    for (auto* bb : rpo) {
        if (bb->getName() == "loop_header")
            loop_header = bb;
        else if (bb->getName() == "loop_body")
            loop_body = bb;
        else if (bb->getName() == "exit")
            exit = bb;
    }

    ASSERT_NE(loop_header, nullptr);
    ASSERT_NE(loop_body, nullptr);
    ASSERT_NE(exit, nullptr);

    // For loop CFG: entry -> loop_header -> {loop_body -> loop_header (back
    // edge), exit} In our implementation, RPO is: entry, loop_header, exit,
    // loop_body This is valid because exit is visited before going into the
    // loop body
    ASSERT_EQ(rpo.size(), 4u);
    EXPECT_EQ(rpo[0], entry);
    EXPECT_EQ(rpo[1], loop_header);
    EXPECT_EQ(rpo[2], exit);
    EXPECT_EQ(rpo[3], loop_body);

    // Verify RPO property: all non-back-edge predecessors come before
    // successors
    size_t headerPos =
        std::find(rpo.begin(), rpo.end(), loop_header) - rpo.begin();
    size_t bodyPos = std::find(rpo.begin(), rpo.end(), loop_body) - rpo.begin();
    size_t exitPos = std::find(rpo.begin(), rpo.end(), exit) - rpo.begin();

    // entry -> loop_header: forward edge
    EXPECT_LT(0u, headerPos);
    // loop_header -> loop_body: forward edge
    EXPECT_LT(headerPos, bodyPos);
    // loop_header -> exit: forward edge
    EXPECT_LT(headerPos, exitPos);
    // loop_body -> loop_header: back edge (should not violate RPO order since
    // header comes first)
    EXPECT_LT(headerPos, bodyPos);  // This is okay for back edges
}

TEST_F(DominanceTest, ComputeDominatorsEfficiency) {
    // Create a larger CFG to test the efficiency improvement of RPO
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* func = Function::Create(fnTy, "efficiency_test", module.get());

    std::vector<BasicBlock*> blocks;

    // Create entry block
    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    blocks.push_back(entry);

    // Create a chain of 20 blocks: entry -> bb1 -> bb2 -> ... -> bb19 -> exit
    for (int i = 1; i <= 19; ++i) {
        auto* bb =
            BasicBlock::Create(context.get(), "bb" + std::to_string(i), func);
        blocks.push_back(bb);
    }

    auto* exit = BasicBlock::Create(context.get(), "exit", func);
    blocks.push_back(exit);

    // Connect them linearly
    IRBuilder builder(entry);
    builder.createBr(blocks[1]);

    for (size_t i = 1; i < blocks.size() - 1; ++i) {
        builder.setInsertPoint(blocks[i]);
        builder.createBr(blocks[i + 1]);
    }

    builder.setInsertPoint(exit);
    builder.createRet(func->getArg(0));

    // Test dominance computation
    DominanceInfo domInfo(func);
    EXPECT_TRUE(domInfo.verify());

    // Entry should dominate all blocks
    for (auto* bb : blocks) {
        EXPECT_TRUE(domInfo.dominates(entry, bb));
    }

    // Each block should dominate all blocks after it
    for (size_t i = 0; i < blocks.size(); ++i) {
        for (size_t j = i; j < blocks.size(); ++j) {
            EXPECT_TRUE(domInfo.dominates(blocks[i], blocks[j]));
        }
        for (size_t j = 0; j < i; ++j) {
            if (i != j) {
                EXPECT_FALSE(domInfo.dominates(blocks[i], blocks[j]));
            }
        }
    }

    // Check immediate dominators
    EXPECT_EQ(domInfo.getImmediateDominator(entry), nullptr);
    for (size_t i = 1; i < blocks.size(); ++i) {
        EXPECT_EQ(domInfo.getImmediateDominator(blocks[i]), blocks[i - 1]);
    }
}

TEST_F(DominanceTest, RPOVsPostOrderRelationship) {
    // Test that RPO is indeed the reverse of post-order traversal
    auto* func = createDiamondFunction();
    DominanceInfo domInfo(func);

    auto rpo = domInfo.computeReversePostOrder();

    // Manually compute post-order using DFS
    std::vector<BasicBlock*> postOrder;
    std::unordered_set<BasicBlock*> visited;

    std::function<void(BasicBlock*)> dfsPostOrder = [&](BasicBlock* bb) {
        if (visited.count(bb)) return;
        visited.insert(bb);

        for (auto* successor : bb->getSuccessors()) {
            if (!visited.count(successor)) {
                dfsPostOrder(successor);
            }
        }
        postOrder.push_back(bb);
    };

    dfsPostOrder(&func->front());

    // RPO should be the reverse of post-order
    std::vector<BasicBlock*> reversedPostOrder(postOrder.rbegin(),
                                               postOrder.rend());

    ASSERT_EQ(rpo.size(), reversedPostOrder.size());
    for (size_t i = 0; i < rpo.size(); ++i) {
        EXPECT_EQ(rpo[i], reversedPostOrder[i])
            << "RPO[" << i << "] = " << rpo[i]->getName()
            << " but reversed post-order[" << i
            << "] = " << reversedPostOrder[i]->getName();
    }
}

TEST_F(DominanceTest, RPOSimpleOrderingTest) {
    // Create a simple test case with known expected RPO ordering
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* func = Function::Create(fnTy, "simple_rpo", module.get());

    // Create CFG: entry -> bb1 -> bb2
    //                  \-> bb3 -> bb2
    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    auto* bb1 = BasicBlock::Create(context.get(), "bb1", func);
    auto* bb2 = BasicBlock::Create(context.get(), "bb2", func);
    auto* bb3 = BasicBlock::Create(context.get(), "bb3", func);

    IRBuilder builder(entry);
    auto* cond =
        builder.createICmpSGT(func->getArg(0), builder.getInt32(0), "cond");
    builder.createCondBr(cond, bb1, bb3);

    builder.setInsertPoint(bb1);
    builder.createBr(bb2);

    builder.setInsertPoint(bb3);
    builder.createBr(bb2);

    builder.setInsertPoint(bb2);
    builder.createRet(func->getArg(0));

    DominanceInfo domInfo(func);
    auto rpo = domInfo.computeReversePostOrder();

    // Expected RPO: entry, bb1, bb3, bb2 (or entry, bb3, bb1, bb2)
    ASSERT_EQ(rpo.size(), 4u);
    EXPECT_EQ(rpo[0], entry);  // Entry must be first
    EXPECT_EQ(rpo[3], bb2);    // bb2 must be last (it's the final merge point)

    // bb1 and bb3 can be in either order, but both must come after entry and
    // before bb2
    bool bb1_before_bb2 = false, bb3_before_bb2 = false;
    bool bb1_after_entry = false, bb3_after_entry = false;

    for (size_t i = 0; i < rpo.size(); ++i) {
        if (rpo[i] == bb1) {
            bb1_before_bb2 = (i < 3);
            bb1_after_entry = (i > 0);
        } else if (rpo[i] == bb3) {
            bb3_before_bb2 = (i < 3);
            bb3_after_entry = (i > 0);
        }
    }

    EXPECT_TRUE(bb1_before_bb2 && bb1_after_entry);
    EXPECT_TRUE(bb3_before_bb2 && bb3_after_entry);
}

}  // namespace